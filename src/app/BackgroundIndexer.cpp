#include "pch.h"
#include "BackgroundIndexer.h"

#include <windows.h>
#include <wil/resource.h>

#include <algorithm>
#include <cwctype>

namespace applocal
{
    namespace
    {
        constexpr uint64_t FilesPerTransaction = 1000;
    }

    BackgroundIndexer::BackgroundIndexer(std::shared_ptr<LocalIndex> index, IndexerOptions options)
        : m_index(std::move(index))
        , m_options(std::move(options))
    {
    }

    BackgroundIndexer::~BackgroundIndexer()
    {
        Stop();
    }

    int64_t BackgroundIndexer::FileTimeToInt64(const FILETIME& ft)
    {
        return (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    bool BackgroundIndexer::ShouldIndexContent(const std::wstring& path, const IndexerOptions& options)
    {
        size_t dot = path.find_last_of(L'.');
        size_t slash = path.find_last_of(L"\\/");
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) || dot + 1 == path.size())
        {
            return false;
        }

        std::wstring ext = path.substr(dot);
        for (auto& c : ext)
        {
            c = static_cast<wchar_t>(towlower(c));
        }

        for (const auto& allowed : options.contentExtensions)
        {
            if (_wcsicmp(ext.c_str(), allowed.c_str()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    bool BackgroundIndexer::LooksBinary(const void* data, size_t length)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < length; ++i)
        {
            if (bytes[i] == 0)
            {
                return true;
            }
        }
        return false;
    }

    void BackgroundIndexer::Start()
    {
        if (m_scannerThread.joinable() || m_contentThread.joinable())
        {
            return; // already running
        }

        m_stopRequested.store(false);
        m_paused.store(false);
        m_scannerThread = std::thread([this] { ScannerThreadProc(); });
        m_contentThread = std::thread([this] { ContentThreadProc(); });
    }

    void BackgroundIndexer::Stop()
    {
        {
            std::scoped_lock lock(m_workMutex, m_pauseMutex, m_queueMutex);
            m_stopRequested.store(true);
            m_paused.store(false);
        }
        m_pauseCv.notify_all();
        m_queueCv.notify_all();
        m_workCv.notify_all();

        if (m_scannerThread.joinable())
        {
            m_scannerThread.join();
        }
        if (m_contentThread.joinable())
        {
            m_contentThread.join();
        }

        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.state = IndexerState::Stopped;
    }

    void BackgroundIndexer::Pause()
    {
        {
            std::lock_guard lock(m_pauseMutex);
            m_paused.store(true);
        }
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.state = IndexerState::Paused;
    }

    void BackgroundIndexer::Resume()
    {
        {
            std::lock_guard lock(m_pauseMutex);
            m_paused.store(false);
        }
        m_pauseCv.notify_all();
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.state = m_scannerActive.load() ? IndexerState::Running : IndexerState::Stopped;
    }

    void BackgroundIndexer::RescanRoot(const std::wstring& root)
    {
        {
            std::lock_guard lock(m_workMutex);
            auto normalized = NormalizeFolder(root);
            if (std::find(m_requestedRoots.begin(), m_requestedRoots.end(), normalized) == m_requestedRoots.end())
                m_requestedRoots.push_back(std::move(normalized));
            std::lock_guard progressLock(m_progressMutex);
            m_progress.reindexStatus = L"Queued";
        }
        m_workCv.notify_one();
    }

    void BackgroundIndexer::RequestReindex()
    {
        {
            std::lock_guard lock(m_workMutex);
            m_rescanAll = true;
            std::lock_guard progressLock(m_progressMutex);
            m_progress.reindexStatus = L"Queued";
        }
        m_workCv.notify_one();
    }

    IndexerProgress BackgroundIndexer::GetProgress() const
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        return m_progress;
    }

    bool BackgroundIndexer::WaitIfPaused()
    {
        std::unique_lock<std::mutex> lock(m_pauseMutex);
        m_pauseCv.wait(lock, [this] { return !m_paused.load() || m_stopRequested.load(); });
        return !m_stopRequested.load();
    }

    std::vector<std::wstring> BackgroundIndexer::GetLocalFixedVolumes() const
    {
        if (!m_options.scanRoots.empty()) return m_options.scanRoots;
        std::vector<std::wstring> volumes;
        DWORD drives = GetLogicalDrives();

        for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
        {
            if (!(drives & (1u << (letter - L'A'))))
            {
                continue;
            }

            std::wstring root = std::wstring(1, letter) + L":\\";
            if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
            {
                continue;
            }

            wchar_t fsName[64] = {};
            if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fsName, ARRAYSIZE(fsName)))
            {
                continue; // e.g. not-ready media; skip rather than fail the whole scan
            }

            if (_wcsicmp(fsName, L"NTFS") != 0)
            {
                continue;
            }

            volumes.push_back(std::move(root));
        }

        return volumes;
    }

    void BackgroundIndexer::EnqueueContentCandidate(std::wstring path)
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_contentQueue.push_back(std::move(path));
        }
        m_queueCv.notify_one();
    }

    void BackgroundIndexer::ScannerThreadProc()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

        bool initial = true;
        while (!m_stopRequested.load())
        {
            bool all = false;
            std::vector<std::wstring> requested;
            {
                std::unique_lock lock(m_workMutex);
                if (!initial)
                    m_workCv.wait(lock, [this] {
                        return m_stopRequested.load() || m_rescanAll || !m_requestedRoots.empty();
                    });
                if (m_stopRequested.load()) break;
                all = std::exchange(m_rescanAll, false);
                requested.swap(m_requestedRoots);
            }
            const bool explicitRequest = all || !requested.empty();
            m_scannerActive.store(true);
            {
                std::lock_guard lock(m_progressMutex);
                m_progress.state = m_paused.load() ? IndexerState::Paused : IndexerState::Running;
                m_progress.filesScanned = 0;
                m_progress.error.clear();
                if (explicitRequest) m_progress.reindexStatus = L"Running";
            }
            m_startTime = std::chrono::steady_clock::now();
            bool failed = false;
            try
            {
                auto volumes = all || requested.empty() ? GetLocalFixedVolumes() : requested;
                auto existingRoots = m_index->GetScanRoots();
                for (const auto& volume : volumes)
                {
                    if (m_stopRequested.load()) break;
                    bool alreadyDone = initial && !explicitRequest &&
                        std::any_of(existingRoots.begin(), existingRoots.end(), [&](const ScanRootStatus& r) {
                            return _wcsicmp(r.root.c_str(), volume.c_str()) == 0 && r.status == L"done";
                        });
                    if (alreadyDone) continue;
                    try { ScanVolume(volume); }
                    catch (const std::exception& error)
                    {
                        failed = true;
                        std::lock_guard lock(m_progressMutex);
                        m_progress.error = volume + L": " +
                            std::wstring(error.what(), error.what() + strlen(error.what()));
                    }
                }
            }
            catch (const std::exception& error)
            {
                failed = true;
                std::lock_guard lock(m_progressMutex);
                m_progress.error = std::wstring(error.what(), error.what() + strlen(error.what()));
            }
            m_scannerActive.store(false);
            {
                std::lock_guard workLock(m_workMutex);
                std::lock_guard lock(m_progressMutex);
                m_progress.state = failed ? IndexerState::Error : IndexerState::Stopped;
                if (m_rescanAll || !m_requestedRoots.empty()) m_progress.reindexStatus = L"Queued";
                else if (failed) m_progress.reindexStatus = L"Error";
                else if (explicitRequest)
                    m_progress.reindexStatus = L"Scan completed; content continues in background";
            }
            initial = false;
        }
    }

    void BackgroundIndexer::ScanVolume(const std::wstring& root)
    {
        int64_t generation = m_index->BeginScanGeneration(root);
        uint64_t totalFiles = 0;
        uint64_t filesInBatch = 0;

        std::vector<std::wstring> dirStack;
        dirStack.push_back(root);

        m_index->BeginTransaction();
        bool transactionOpen = true;

        bool stoppedEarly = false;
        bool incomplete = false;

        try
        {
            while (!dirStack.empty())
            {
                if (m_stopRequested.load())
                {
                    stoppedEarly = true;
                    break;
                }
                if (!WaitIfPaused())
                {
                    stoppedEarly = true;
                    break;
                }

                std::wstring dir = std::move(dirStack.back());
                dirStack.pop_back();

                {
                    std::lock_guard<std::mutex> lock(m_progressMutex);
                    m_progress.currentPath = dir;
                }

                std::wstring pattern = dir;
                if (!pattern.empty() && pattern.back() != L'\\')
                {
                    pattern += L'\\';
                }
                pattern += L'*';

                WIN32_FIND_DATAW findData;
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &findData);
                if (hFind == INVALID_HANDLE_VALUE)
                {
                    if (GetLastError() != ERROR_FILE_NOT_FOUND) incomplete = true;
                    continue;
                }
                auto closeFind = wil::scope_exit([&] { FindClose(hFind); });

                do
                {
                    std::wstring_view name(findData.cFileName);
                    if (name == L"." || name == L"..")
                    {
                        continue;
                    }

                    std::wstring fullPath = dir;
                    if (!fullPath.empty() && fullPath.back() != L'\\')
                    {
                        fullPath += L'\\';
                    }
                    fullPath += findData.cFileName;

                    bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    bool isReparse = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

                    ULARGE_INTEGER size;
                    size.LowPart = findData.nFileSizeLow;
                    size.HighPart = findData.nFileSizeHigh;

                    m_index->UpsertFile(
                        fullPath,
                        findData.cFileName,
                        dir,
                        isDir,
                        size.QuadPart,
                        findData.dwFileAttributes,
                        FileTimeToInt64(findData.ftCreationTime),
                        FileTimeToInt64(findData.ftLastWriteTime),
                        FileTimeToInt64(findData.ftLastAccessTime),
                        generation);

                    ++totalFiles;
                    ++filesInBatch;

                    if (isDir)
                    {
                        if (!isReparse || m_options.followReparsePoints)
                        {
                            dirStack.push_back(fullPath);
                        }
                    }
                    else if (ShouldIndexContent(fullPath, m_options))
                    {
                        EnqueueContentCandidate(fullPath);
                    }

                    if (filesInBatch >= FilesPerTransaction)
                    {
                        m_index->EndTransaction(true);
                        transactionOpen = false;
                        m_index->BeginTransaction();
                        transactionOpen = true;
                        filesInBatch = 0;

                        {
                            std::lock_guard<std::mutex> lock(m_progressMutex);
                            m_progress.filesScanned = totalFiles;
                            m_progress.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - m_startTime);
                        }

                        // Yield generously between batches so this stays a
                        // background-priority citizen even on machines
                        // where thread-priority alone isn't enough to keep
                        // the UI feeling instant.
                        Sleep(1);
                    }
                } while (!m_stopRequested.load() && FindNextFileW(hFind, &findData));

                if (!m_stopRequested.load() && GetLastError() != ERROR_NO_MORE_FILES)
                    incomplete = true;
            }

            if (transactionOpen)
            {
                m_index->EndTransaction(true);
                transactionOpen = false;
            }
        }
        catch (...)
        {
            if (transactionOpen)
            {
                m_index->EndTransaction(false);
            }
            throw;
        }

        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            m_progress.filesScanned = totalFiles;
            m_progress.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_startTime);
        }

        if (!stoppedEarly && !m_stopRequested.load() && !incomplete)
        {
            m_index->SweepStale(root, generation);
            m_index->SetScanRootStatus(root, L"done", generation, totalFiles);
        }
        else
        {
            // Leave it "pending" so the next Start() resumes by re-walking
            // this volume (v1 resume granularity is per-volume, not
            // per-file/per-directory).
            m_index->SetScanRootStatus(root, incomplete ? L"incomplete" : L"pending", 0, totalFiles);
            if (incomplete)
                throw std::runtime_error("Scan incomplete: inaccessible directories or enumeration errors; retained old rows without sweeping");
        }
    }

    void BackgroundIndexer::ContentThreadProc()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

        while (true)
        {
            std::wstring path;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [this]
                    {
                        return !m_contentQueue.empty() || m_stopRequested.load();
                    });

                if (m_contentQueue.empty())
                {
                    if (m_stopRequested.load())
                    {
                        break;
                    }
                    continue;
                }

                path = std::move(m_contentQueue.front());
                m_contentQueue.pop_front();
            }

            if (m_stopRequested.load())
            {
                break;
            }
            if (!WaitIfPaused())
            {
                break;
            }

            try
            {
                IndexFileContent(path);
            }
            catch (...)
            {
                // Skip unreadable/locked/transient files; content indexing
                // is best-effort and must never take down the thread.
            }

            Sleep(1);
        }
    }

    void BackgroundIndexer::IndexFileContent(const std::wstring& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA attrData{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrData))
        {
            return;
        }

        int64_t mtime = FileTimeToInt64(attrData.ftLastWriteTime);
        ULARGE_INTEGER sizeLI;
        sizeLI.LowPart = attrData.nFileSizeLow;
        sizeLI.HighPart = attrData.nFileSizeHigh;
        uint64_t size = sizeLI.QuadPart;

        if (size == 0 || size > m_options.maxContentFileSizeBytes)
        {
            return;
        }

        if (m_index->IsContentUpToDate(path, mtime, size))
        {
            return;
        }

        HANDLE hFile = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            return;
        }
        auto closeFile = wil::scope_exit([&] { CloseHandle(hFile); });

        std::vector<char> buffer(static_cast<size_t>(size));
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
        {
            return;
        }
        buffer.resize(bytesRead);

        if (LooksBinary(buffer.data(), (std::min)(buffer.size(), size_t(8192))))
        {
            return; // binary sniff: skip even though the extension matched
        }

        // Decode as UTF-8; fall back to a naive byte->wchar_t widen for
        // files that aren't valid UTF-8 (still lets FTS5 tokenize ASCII
        // content sensibly even for odd encodings).
        std::wstring text;
        int wideLen = buffer.empty() ? 0 : MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, buffer.data(), static_cast<int>(buffer.size()), nullptr, 0);
        if (wideLen > 0)
        {
            text.resize(static_cast<size_t>(wideLen));
            MultiByteToWideChar(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), text.data(), wideLen);
        }
        else
        {
            text.reserve(buffer.size());
            for (unsigned char c : buffer)
            {
                text.push_back(static_cast<wchar_t>(c));
            }
        }

        std::vector<std::wstring> chunks;
        for (size_t offset = 0; offset < text.size(); offset += m_options.chunkSizeChars)
        {
            chunks.push_back(text.substr(offset, m_options.chunkSizeChars));
        }
        if (chunks.empty())
        {
            chunks.emplace_back();
        }

        m_index->UpsertContent(path, chunks, mtime, size, /*truncated*/ false);
    }
}
