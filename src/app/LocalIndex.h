// LocalIndex.h - App-local (not src/api) SQLite/FTS5 "true index" engine.
//
// This is the persistent on-disk index built by BackgroundIndexer.h by
// walking every local NTFS volume with FindFirstFileW/FindNextFileW. It is
// a superset of whatever the Windows Search indexer scope covers, and is
// queried alongside (not instead of) wsearch::SearchAsYouTypeSession - see
// MainWindow.xaml.cpp's ExecuteSearchAsync for the merge point.
//
// Not part of the public src/api contract: this header is app-specific.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "IndexSettings.h"

struct sqlite3;

namespace applocal
{
    // How a LocalIndex-backed result matched the query. (The richer
    // SearchApp::MatchKind WinRT enum - which also has a Metadata tier for
    // indexer-backed results - is derived from this in MainWindow.xaml.cpp.)
    enum class LocalMatchKind
    {
        Filename,
        Content,
    };

    struct LocalSearchResult
    {
        std::wstring path;
        std::wstring name;
        bool isFolder = false;
        uint64_t size = 0;
        int64_t dateCreated = 0;   // FILETIME, as int64
        int64_t dateModified = 0;  // FILETIME, as int64
        LocalMatchKind matchKind = LocalMatchKind::Filename;
    };

    struct ScanRootStatus
    {
        std::wstring root;
        std::wstring status; // "pending" | "in_progress" | "done"
        int64_t lastFullScan = 0;
        uint64_t filesScanned = 0;
    };

    struct IndexStatistics
    {
        uint64_t totalItems = 0;
        uint64_t files = 0;
        uint64_t folders = 0;
        uint64_t contentIndexedFiles = 0;
    };

    // Thin RAII wrapper around the on-disk SQLite/FTS5 index. Owns a single
    // writer connection (serialized via m_writeMutex - BackgroundIndexer is
    // the only writer) and a second, independent read-only connection so
    // live per-keystroke queries are never blocked behind the indexer's
    // bulk writes.
    class LocalIndex
    {
    public:
        explicit LocalIndex(std::wstring dbPath);
        ~LocalIndex();

        LocalIndex(const LocalIndex&) = delete;
        LocalIndex& operator=(const LocalIndex&) = delete;

        // %LOCALAPPDATA%\SearchApp\index.db
        static std::wstring DefaultDbPath();

        // Name+content search against the local index only; the caller
        // (MainWindow) merges this with indexer-backed results. Filename
        // hits are returned before content hits; each source is separately
        // capped so a single very-common word can't crowd out filename
        // matches.
        std::vector<LocalSearchResult> Search(const std::wstring& queryText, uint32_t maxResults) const;
        std::vector<LocalSearchResult> Search(const std::wstring& queryText, uint32_t maxResults,
            const IndexSettings& settings) const;
        IndexSettings GetSettings() const;
        void AddExclusion(const std::wstring& kind, const std::wstring& value);
        void RemoveExclusion(const std::wstring& kind, const std::wstring& value);
        void SetContentScope(const ContentScope& scope);
        void ForgetContentScope(const std::wstring& folder);
        void RecoverInterruptedScopeRequests();

        // --- Writer-side API used by BackgroundIndexer ---

        void UpsertFile(
            const std::wstring& path,
            const std::wstring& name,
            const std::wstring& dirPath,
            bool isFolder,
            uint64_t size,
            uint64_t attributes,
            int64_t dateCreated,
            int64_t dateModified,
            int64_t dateAccessed,
            int64_t scanGeneration);

        // Returns true (and skips re-indexing) when content_meta already
        // has an up-to-date row for this path (same mtime+size).
        bool IsContentUpToDate(const std::wstring& path, int64_t mtime, uint64_t size) const;

        void UpsertContent(
            const std::wstring& path,
            const std::vector<std::wstring>& chunks,
            int64_t mtime,
            uint64_t size,
            bool truncated);

        void DeleteContent(const std::wstring& path);

        // Scan generations use a persisted monotonic sequence seeded by
        // FILETIME (also safe for rapid requests or clock rollback). Every file
        // touched during a walk of `root` is stamped with this value;
        // SweepStale() then deletes anything under `root` that has an
        // older stamp (i.e. wasn't seen this walk - handles deletes/renames).
        int64_t BeginScanGeneration(const std::wstring& root);
        void SweepStale(const std::wstring& root, int64_t scanGeneration);

        void SetScanRootStatus(const std::wstring& root, const std::wstring& status, int64_t lastFullScan, uint64_t filesScanned);
        std::vector<ScanRootStatus> GetScanRoots() const;
        IndexStatistics GetStatistics() const;

        // Batches many Upsert* calls into a single SQLite transaction (huge
        // throughput win for a full-volume walk vs. one implicit transaction
        // per row). BeginTransaction() acquires m_writeMutex and holds it
        // until EndTransaction() - safe because m_writeMutex is recursive,
        // so nested Upsert*() calls from the same (sole writer) thread just
        // re-enter it. EndTransaction(false) rolls back (e.g. on exception).
        void BeginTransaction();
        void EndTransaction(bool commit);

    private:
        void RunMigrations();

        sqlite3* m_write = nullptr;
        sqlite3* m_read = nullptr;         // interactive search
        sqlite3* m_contentRead = nullptr;  // background content freshness checks
        sqlite3* m_statsRead = nullptr;    // dashboard/root status
        sqlite3* m_settings = nullptr;     // settings never share the scanner's transaction
        mutable std::mutex m_settingsMutex;
        mutable std::mutex m_searchMutex;
        mutable std::recursive_mutex m_writeMutex;
    };
}
