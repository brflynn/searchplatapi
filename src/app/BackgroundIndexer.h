// BackgroundIndexer.h - "true index" background full-filesystem walker.
//
// Builds/maintains the LocalIndex (SQLite/FTS5) engine by recursively
// walking every local fixed NTFS volume with FindFirstFileW/FindNextFileW,
// running at idle thread priority so it stays out of the way of the
// foreground search UI. A second thread performs content indexing for a
// configurable set of text-like extensions once the name index for a path
// has been written.
//
// Not part of the public src/api contract: this header is app-specific.
#pragma once

#include "LocalIndex.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace applocal
{
    enum class IndexerState
    {
        Stopped,
        Running,
        Paused,
        Error,
    };

    struct IndexerOptions
    {
        // Empty selects all fixed NTFS volumes. Explicit roots support isolated tests.
        std::vector<std::wstring> scanRoots;
        // Skip FILE_ATTRIBUTE_REPARSE_POINT directories by default to avoid
        // cycles (junctions/symlinks can point back into already-scanned
        // trees). Configurable per the requirements.
        bool followReparsePoints = false;

        // Text-like/known extensions considered for phase-2 content
        // indexing. Case-insensitive; configurable.
        std::vector<std::wstring> contentExtensions = {
            L".txt", L".md", L".markdown", L".cs", L".cpp", L".cc", L".c",
            L".h", L".hpp", L".hh", L".json", L".xml", L".log", L".ini",
            L".cfg", L".conf", L".yaml", L".yml", L".py", L".js", L".ts",
            L".jsx", L".tsx", L".html", L".htm", L".css", L".sql", L".ps1",
            L".bat", L".sh", L".csv", L".rtf",
        };

        // Files larger than this are skipped for content indexing (name is
        // still indexed regardless of size).
        uint64_t maxContentFileSizeBytes = 8ull * 1024 * 1024;

        // Content is split into roughly this many UTF-16 characters per
        // FTS5 row, so very large files still produce bounded-size rows.
        size_t chunkSizeChars = 4000;
    };

    struct IndexerProgress
    {
        IndexerState state = IndexerState::Stopped;
        uint64_t filesScanned = 0;
        std::wstring currentPath;
        std::chrono::milliseconds elapsed{ 0 };
        std::wstring reindexStatus = L"Ready";
        std::wstring error;
    };

    // Owns two background-priority worker threads (scanner + content
    // indexer) that populate a shared LocalIndex. Safe to construct/destruct
    // from the UI thread; requests/progress never join workers. Stop() and
    // destruction join, so the window transfers ownership to a background task on close.
    class BackgroundIndexer
    {
    public:
        explicit BackgroundIndexer(std::shared_ptr<LocalIndex> index, IndexerOptions options = {});
        ~BackgroundIndexer();

        BackgroundIndexer(const BackgroundIndexer&) = delete;
        BackgroundIndexer& operator=(const BackgroundIndexer&) = delete;

        // Starts the scanner + content threads if not already running.
        // Volumes whose scan_roots status is already "done" are skipped
        // (resume-without-full-rescan); call RescanRoot() first to force a
        // specific volume to be walked again.
        void Start();

        // Signals both threads to stop and joins them. Safe to call even if
        // not running (no-op). A subsequent Start() resumes from whatever
        // scan_roots state was persisted (in-progress roots are re-walked
        // from scratch - v1 resume granularity is per-volume, not per-file).
        void Stop();

        // Pauses/resumes without tearing down threads; checked at
        // directory/file granularity so it takes effect promptly.
        void Pause();
        void Resume();

        // Queues a root on the existing persistent scanner, even while scanning.
        void RescanRoot(const std::wstring& root);
        void RequestReindex(); // coalesced full-volume rescan, no DB writes or joins here

        IndexerProgress GetProgress() const;

        // Exposed for unit testing (src/test) without needing real volumes
        // or a running indexer.
        static bool ShouldIndexContent(const std::wstring& path, const IndexerOptions& options);
        static bool LooksBinary(const void* data, size_t length);
        static int64_t FileTimeToInt64(const FILETIME& ft);

    private:
        void ScannerThreadProc();
        void ContentThreadProc();
        void ScanVolume(const std::wstring& root);
        void IndexFileContent(const std::wstring& path);
        void EnqueueContentCandidate(std::wstring path);
        std::vector<std::wstring> GetLocalFixedVolumes() const;

        // Blocks while paused; returns false if a stop was requested while
        // waiting (caller should unwind), true if it's fine to continue.
        bool WaitIfPaused();

        std::shared_ptr<LocalIndex> m_index;
        IndexerOptions m_options;

        std::thread m_scannerThread;
        std::thread m_contentThread;

        std::atomic<bool> m_stopRequested{ false };
        std::atomic<bool> m_paused{ false };
        std::atomic<bool> m_scannerActive{ false };
        std::condition_variable m_pauseCv;
        std::mutex m_pauseMutex;

        mutable std::mutex m_progressMutex;
        IndexerProgress m_progress;
        std::chrono::steady_clock::time_point m_startTime;

        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::deque<std::wstring> m_contentQueue;
        std::mutex m_workMutex;
        std::condition_variable m_workCv;
        bool m_rescanAll = false;
        std::vector<std::wstring> m_requestedRoots;
    };
}
