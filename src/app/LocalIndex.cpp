#include "pch.h"
#include "LocalIndex.h"
#include "thirdparty/sqlite/sqlite3.h"

#include <stdexcept>
#include <windows.h>

namespace applocal
{
    namespace
    {
        // RAII helper for sqlite3_stmt*.
        class Stmt
        {
        public:
            Stmt(sqlite3* db, const wchar_t* sql)
            {
                if (sqlite3_prepare16_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
                {
                    throw std::runtime_error("sqlite3_prepare16_v2 failed");
                }
            }

            ~Stmt()
            {
                sqlite3_finalize(m_stmt);
            }

            Stmt(const Stmt&) = delete;
            Stmt& operator=(const Stmt&) = delete;

            operator sqlite3_stmt* () const { return m_stmt; }

            void BindText(int index, const std::wstring& value)
            {
                sqlite3_bind_text16(m_stmt, index, value.c_str(),
                    static_cast<int>(value.size() * sizeof(wchar_t)), SQLITE_TRANSIENT);
            }

            void BindInt64(int index, int64_t value)
            {
                sqlite3_bind_int64(m_stmt, index, value);
            }

            void BindInt(int index, int value)
            {
                sqlite3_bind_int(m_stmt, index, value);
            }

            int Step()
            {
                int rc = sqlite3_step(m_stmt);
                if (rc != SQLITE_ROW && rc != SQLITE_DONE)
                {
                    // Surface constraint violations etc. (e.g. UpsertContent
                    // called for a path not yet present in `files`, which
                    // violates content_meta's FK) instead of silently
                    // discarding the write - a previously-swallowed error
                    // here meant "content indexed" could quietly no-op.
                    throw std::runtime_error("sqlite3_step failed");
                }
                return rc;
            }

            std::wstring ColumnText(int index) const
            {
                const void* text = sqlite3_column_text16(m_stmt, index);
                if (!text)
                {
                    return {};
                }
                int bytes = sqlite3_column_bytes16(m_stmt, index);
                return std::wstring(static_cast<const wchar_t*>(text), bytes / sizeof(wchar_t));
            }

            int64_t ColumnInt64(int index) const
            {
                return sqlite3_column_int64(m_stmt, index);
            }

            int ColumnInt(int index) const
            {
                return sqlite3_column_int(m_stmt, index);
            }

        private:
            sqlite3_stmt* m_stmt = nullptr;
        };

        void ExecOrThrow(sqlite3* db, const char* sql)
        {
            char* errMsg = nullptr;
            int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK)
            {
                std::string message = errMsg ? errMsg : "sqlite3_exec failed";
                sqlite3_free(errMsg);
                throw std::runtime_error(message);
            }
        }

        // Builds a simple prefix-match FTS5 query from free-typed text:
        // "foo bar" -> "\"foo\"* \"bar\"*" (each token becomes an AND'd
        // prefix match). This mirrors the incremental/substring-ish feel of
        // Everything-style search well enough for v1 (FTS5 itself can only
        // do token-prefix matches, not arbitrary substrings).
        std::wstring BuildFtsPrefixQuery(const std::wstring& queryText)
        {
            std::wstring result;
            std::wstring token;
            auto flushToken = [&]()
            {
                if (token.empty())
                {
                    return;
                }
                if (!result.empty())
                {
                    result += L' ';
                }
                result += L'"';
                for (wchar_t c : token)
                {
                    if (c == L'"')
                    {
                        result += L'"'; // escape embedded quotes by doubling
                    }
                    result += c;
                }
                result += L"\"*";
                token.clear();
            };

            for (wchar_t c : queryText)
            {
                if (iswspace(c))
                {
                    flushToken();
                }
                else
                {
                    token += c;
                }
            }
            flushToken();
            return result;
        }
    }

    LocalIndex::LocalIndex(std::wstring dbPath)
    {
        // Ensure the parent directory exists (e.g. %LOCALAPPDATA%\SearchApp).
        size_t slash = dbPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            std::wstring dir = dbPath.substr(0, slash);
            CreateDirectoryW(dir.c_str(), nullptr);
        }

        if (sqlite3_open16(dbPath.c_str(), &m_write) != SQLITE_OK)
        {
            throw std::runtime_error("failed to open LocalIndex write connection");
        }
        sqlite3_busy_timeout(m_write, 5000);

        // Run schema migrations (including enabling WAL) on the write
        // connection *before* opening the read connection below, so the
        // reader always connects to an already-WAL-mode database - opening
        // a read-only connection ahead of WAL being enabled can leave it
        // unable to see the -wal file's committed-but-not-checkpointed
        // rows (visible as "just-written data isn't found by Search()").
        RunMigrations();

        // Read connection: read-only so it isn't blocked for long behind
        // the writer's bulk transactions. WAL mode (enabled above) lets
        // this connection see the writer's commits without blocking on it.
        std::string utf8Path;
        {
            int needed = WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            utf8Path.resize(needed > 0 ? static_cast<size_t>(needed) - 1 : 0);
            if (needed > 0)
            {
                WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, utf8Path.data(), needed, nullptr, nullptr);
            }
        }
        auto openReadConnection = [&](sqlite3** connection)
        {
            if (sqlite3_open_v2(
                utf8Path.c_str(), connection, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            {
                if (*connection)
                {
                    sqlite3_close(*connection);
                    *connection = nullptr;
                }
            }
            if (*connection)
            {
                sqlite3_busy_timeout(*connection, 5000);
            }
        };
        openReadConnection(&m_read);
        openReadConnection(&m_contentRead);
        openReadConnection(&m_statsRead);
    }

    LocalIndex::~LocalIndex()
    {
        if (m_statsRead) sqlite3_close(m_statsRead);
        if (m_contentRead) sqlite3_close(m_contentRead);
        if (m_read) sqlite3_close(m_read);
        if (m_write) sqlite3_close(m_write);
    }

    std::wstring LocalIndex::DefaultDbPath()
    {
        wchar_t* localAppData = nullptr;
        std::wstring result;
        if (_wdupenv_s(&localAppData, nullptr, L"LOCALAPPDATA") == 0 && localAppData)
        {
            result = localAppData;
            free(localAppData);
        }
        if (result.empty())
        {
            result = L"."; // last-resort fallback
        }
        return result + L"\\SearchApp\\index.db";
    }

    void LocalIndex::RunMigrations()
    {
        std::lock_guard<std::recursive_mutex> lock(m_writeMutex);

        ExecOrThrow(m_write, "PRAGMA journal_mode=WAL;");
        ExecOrThrow(m_write, "PRAGMA synchronous=NORMAL;");
        ExecOrThrow(m_write, "PRAGMA foreign_keys=ON;");

        ExecOrThrow(m_write,
            "CREATE TABLE IF NOT EXISTS files ("
            "  id            INTEGER PRIMARY KEY,"
            "  path          TEXT NOT NULL UNIQUE,"
            "  name          TEXT NOT NULL,"
            "  dir_path      TEXT NOT NULL,"
            "  is_folder     INTEGER NOT NULL,"
            "  size          INTEGER NOT NULL,"
            "  attributes    INTEGER NOT NULL,"
            "  date_created  INTEGER NOT NULL,"
            "  date_modified INTEGER NOT NULL,"
            "  date_accessed INTEGER NOT NULL,"
            "  last_seen_scan INTEGER NOT NULL"
            ");");
        ExecOrThrow(m_write, "CREATE INDEX IF NOT EXISTS idx_files_dir ON files(dir_path);");

        ExecOrThrow(m_write,
            "CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5("
            "  name, content='files', content_rowid='id', tokenize='unicode61'"
            ");");

        ExecOrThrow(m_write,
            "CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN"
            "  INSERT INTO files_fts(rowid, name) VALUES (new.id, new.name);"
            "END;");
        ExecOrThrow(m_write,
            "CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN"
            "  INSERT INTO files_fts(files_fts, rowid, name) VALUES('delete', old.id, old.name);"
            "END;");
        ExecOrThrow(m_write,
            "CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN"
            "  INSERT INTO files_fts(files_fts, rowid, name) VALUES('delete', old.id, old.name);"
            "  INSERT INTO files_fts(rowid, name) VALUES (new.id, new.name);"
            "END;");

        ExecOrThrow(m_write,
            "CREATE TABLE IF NOT EXISTS content_meta ("
            "  path        TEXT PRIMARY KEY REFERENCES files(path) ON DELETE CASCADE,"
            "  mtime       INTEGER NOT NULL,"
            "  size        INTEGER NOT NULL,"
            "  indexed_at  INTEGER NOT NULL,"
            "  truncated   INTEGER NOT NULL"
            ");");

        ExecOrThrow(m_write,
            "CREATE VIRTUAL TABLE IF NOT EXISTS content_fts USING fts5("
            "  path UNINDEXED, chunk_index UNINDEXED, content, tokenize='unicode61'"
            ");");

        ExecOrThrow(m_write,
            "CREATE TRIGGER IF NOT EXISTS content_meta_ad AFTER DELETE ON content_meta BEGIN"
            "  DELETE FROM content_fts WHERE path = old.path;"
            "END;");

        ExecOrThrow(m_write,
            "CREATE TABLE IF NOT EXISTS scan_roots ("
            "  root TEXT PRIMARY KEY,"
            "  status TEXT NOT NULL,"
            "  last_full_scan INTEGER,"
            "  files_scanned INTEGER NOT NULL DEFAULT 0"
            ");");
    }

    std::vector<LocalSearchResult> LocalIndex::Search(const std::wstring& queryText, uint32_t maxResults) const
    {
        std::vector<LocalSearchResult> results;
        if (queryText.empty() || !m_read)
        {
            return results;
        }

        std::wstring ftsQuery = BuildFtsPrefixQuery(queryText);
        if (ftsQuery.empty())
        {
            return results;
        }

        std::vector<std::wstring> seenPaths;
        auto alreadySeen = [&](const std::wstring& path)
        {
            for (auto& p : seenPaths)
            {
                if (_wcsicmp(p.c_str(), path.c_str()) == 0) return true;
            }
            return false;
        };

        try
        {
            // Filename hits first.
            Stmt nameStmt(m_read,
                L"SELECT f.path, f.name, f.is_folder, f.size, f.date_created, f.date_modified "
                L"FROM files_fts JOIN files f ON f.id = files_fts.rowid "
                L"WHERE files_fts MATCH ?1 ORDER BY bm25(files_fts) LIMIT ?2;");
            nameStmt.BindText(1, ftsQuery);
            nameStmt.BindInt(2, static_cast<int>(maxResults));
            while (nameStmt.Step() == SQLITE_ROW && results.size() < maxResults)
            {
                LocalSearchResult r;
                r.path = nameStmt.ColumnText(0);
                r.name = nameStmt.ColumnText(1);
                r.isFolder = nameStmt.ColumnInt(2) != 0;
                r.size = static_cast<uint64_t>(nameStmt.ColumnInt64(3));
                r.dateCreated = nameStmt.ColumnInt64(4);
                r.dateModified = nameStmt.ColumnInt64(5);
                r.matchKind = LocalMatchKind::Filename;
                seenPaths.push_back(r.path);
                results.push_back(std::move(r));
            }

            // Content hits fill any remaining room.
            if (results.size() < maxResults)
            {
                Stmt contentStmt(m_read,
                    L"SELECT f.path, f.name, f.is_folder, f.size, f.date_created, f.date_modified "
                    L"FROM content_fts c JOIN files f ON f.path = c.path "
                    L"WHERE content_fts MATCH ?1 GROUP BY f.path LIMIT ?2;");
                contentStmt.BindText(1, ftsQuery);
                contentStmt.BindInt(2, static_cast<int>(maxResults));
                while (contentStmt.Step() == SQLITE_ROW && results.size() < maxResults)
                {
                    LocalSearchResult r;
                    r.path = contentStmt.ColumnText(0);
                    if (alreadySeen(r.path))
                    {
                        continue;
                    }
                    r.name = contentStmt.ColumnText(1);
                    r.isFolder = contentStmt.ColumnInt(2) != 0;
                    r.size = static_cast<uint64_t>(contentStmt.ColumnInt64(3));
                    r.dateCreated = contentStmt.ColumnInt64(4);
                    r.dateModified = contentStmt.ColumnInt64(5);
                    r.matchKind = LocalMatchKind::Content;
                    seenPaths.push_back(r.path);
                    results.push_back(std::move(r));
                }
            }
        }
        catch (const std::exception&)
        {
            // Local index unavailable/corrupt: degrade gracefully to
            // indexer-only results rather than surfacing an error to the UI.
        }

        return results;
    }

    void LocalIndex::UpsertFile(
        const std::wstring& path,
        const std::wstring& name,
        const std::wstring& dirPath,
        bool isFolder,
        uint64_t size,
        uint64_t attributes,
        int64_t dateCreated,
        int64_t dateModified,
        int64_t dateAccessed,
        int64_t scanGeneration)
    {
        std::lock_guard<std::recursive_mutex> lock(m_writeMutex);
        Stmt stmt(m_write,
            L"INSERT INTO files(path, name, dir_path, is_folder, size, attributes, "
            L"date_created, date_modified, date_accessed, last_seen_scan) "
            L"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
            L"ON CONFLICT(path) DO UPDATE SET "
            L"  name=excluded.name, dir_path=excluded.dir_path, is_folder=excluded.is_folder, "
            L"  size=excluded.size, attributes=excluded.attributes, "
            L"  date_created=excluded.date_created, date_modified=excluded.date_modified, "
            L"  date_accessed=excluded.date_accessed, last_seen_scan=excluded.last_seen_scan;");
        stmt.BindText(1, path);
        stmt.BindText(2, name);
        stmt.BindText(3, dirPath);
        stmt.BindInt(4, isFolder ? 1 : 0);
        stmt.BindInt64(5, static_cast<int64_t>(size));
        stmt.BindInt64(6, static_cast<int64_t>(attributes));
        stmt.BindInt64(7, dateCreated);
        stmt.BindInt64(8, dateModified);
        stmt.BindInt64(9, dateAccessed);
        stmt.BindInt64(10, scanGeneration);
        stmt.Step();
    }

    bool LocalIndex::IsContentUpToDate(const std::wstring& path, int64_t mtime, uint64_t size) const
    {
        sqlite3* connection = m_contentRead ? m_contentRead : m_read;
        if (!connection)
        {
            return false;
        }
        Stmt stmt(connection, L"SELECT mtime, size FROM content_meta WHERE path = ?1;");
        stmt.BindText(1, path);
        if (stmt.Step() == SQLITE_ROW)
        {
            return stmt.ColumnInt64(0) == mtime && static_cast<uint64_t>(stmt.ColumnInt64(1)) == size;
        }
        return false;
    }

    void LocalIndex::UpsertContent(
        const std::wstring& path,
        const std::vector<std::wstring>& chunks,
        int64_t mtime,
        uint64_t size,
        bool truncated)
    {
        std::lock_guard<std::recursive_mutex> lock(m_writeMutex);

        DeleteContent(path);

        {
            Stmt meta(m_write,
                L"INSERT INTO content_meta(path, mtime, size, indexed_at, truncated) "
                L"VALUES (?1, ?2, ?3, ?4, ?5) "
                L"ON CONFLICT(path) DO UPDATE SET mtime=excluded.mtime, size=excluded.size, "
                L"  indexed_at=excluded.indexed_at, truncated=excluded.truncated;");
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            int64_t now = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            meta.BindText(1, path);
            meta.BindInt64(2, mtime);
            meta.BindInt64(3, static_cast<int64_t>(size));
            meta.BindInt64(4, now);
            meta.BindInt(5, truncated ? 1 : 0);
            meta.Step();
        }

        int chunkIndex = 0;
        for (const auto& chunk : chunks)
        {
            Stmt insert(m_write, L"INSERT INTO content_fts(path, chunk_index, content) VALUES (?1, ?2, ?3);");
            insert.BindText(1, path);
            insert.BindInt(2, chunkIndex++);
            insert.BindText(3, chunk);
            insert.Step();
        }
    }

    void LocalIndex::DeleteContent(const std::wstring& path)
    {
        // Caller already holds m_writeMutex when invoked from UpsertContent;
        // also safe to call standalone.
        Stmt delFts(m_write, L"DELETE FROM content_fts WHERE path = ?1;");
        delFts.BindText(1, path);
        delFts.Step();

        Stmt delMeta(m_write, L"DELETE FROM content_meta WHERE path = ?1;");
        delMeta.BindText(1, path);
        delMeta.Step();
    }

    int64_t LocalIndex::BeginScanGeneration(const std::wstring& root)
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        int64_t generation = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

        SetScanRootStatus(root, L"in_progress", 0, 0);
        return generation;
    }

    void LocalIndex::SweepStale(const std::wstring& root, int64_t scanGeneration)
    {
        std::lock_guard<std::recursive_mutex> lock(m_writeMutex);
        Stmt stmt(m_write, L"DELETE FROM files WHERE (path = ?1 OR path LIKE ?2) AND last_seen_scan < ?3;");
        stmt.BindText(1, root);
        stmt.BindText(2, root + L"%");
        stmt.BindInt64(3, scanGeneration);
        stmt.Step();
    }

    void LocalIndex::SetScanRootStatus(const std::wstring& root, const std::wstring& status, int64_t lastFullScan, uint64_t filesScanned)
    {
        std::lock_guard<std::recursive_mutex> lock(m_writeMutex);
        Stmt stmt(m_write,
            L"INSERT INTO scan_roots(root, status, last_full_scan, files_scanned) VALUES (?1, ?2, ?3, ?4) "
            L"ON CONFLICT(root) DO UPDATE SET status=excluded.status, "
            L"  last_full_scan = CASE WHEN excluded.last_full_scan != 0 THEN excluded.last_full_scan ELSE scan_roots.last_full_scan END, "
            L"  files_scanned=excluded.files_scanned;");
        stmt.BindText(1, root);
        stmt.BindText(2, status);
        stmt.BindInt64(3, lastFullScan);
        stmt.BindInt64(4, static_cast<int64_t>(filesScanned));
        stmt.Step();
    }

    std::vector<ScanRootStatus> LocalIndex::GetScanRoots() const
    {
        std::vector<ScanRootStatus> roots;
        sqlite3* connection = m_statsRead ? m_statsRead : m_read;
        if (!connection)
        {
            return roots;
        }
        Stmt stmt(connection, L"SELECT root, status, last_full_scan, files_scanned FROM scan_roots;");
        while (stmt.Step() == SQLITE_ROW)
        {
            ScanRootStatus s;
            s.root = stmt.ColumnText(0);
            s.status = stmt.ColumnText(1);
            s.lastFullScan = stmt.ColumnInt64(2);
            s.filesScanned = static_cast<uint64_t>(stmt.ColumnInt64(3));
            roots.push_back(std::move(s));
        }
        return roots;
    }

    IndexStatistics LocalIndex::GetStatistics() const
    {
        IndexStatistics statistics;
        sqlite3* connection = m_statsRead ? m_statsRead : m_read;
        if (!connection)
        {
            return statistics;
        }

        Stmt stmt(connection,
            L"SELECT "
            L"  COUNT(*), "
            L"  COALESCE(SUM(CASE WHEN is_folder = 0 THEN 1 ELSE 0 END), 0), "
            L"  COALESCE(SUM(CASE WHEN is_folder != 0 THEN 1 ELSE 0 END), 0), "
            L"  (SELECT COUNT(*) FROM content_meta) "
            L"FROM files;");

        if (stmt.Step() == SQLITE_ROW)
        {
            statistics.totalItems = static_cast<uint64_t>(stmt.ColumnInt64(0));
            statistics.files = static_cast<uint64_t>(stmt.ColumnInt64(1));
            statistics.folders = static_cast<uint64_t>(stmt.ColumnInt64(2));
            statistics.contentIndexedFiles = static_cast<uint64_t>(stmt.ColumnInt64(3));
        }

        return statistics;
    }

    void LocalIndex::BeginTransaction()
    {
        // Held until EndTransaction(); safe to re-enter from nested
        // Upsert*() calls made by the same (sole writer) thread because
        // m_writeMutex is a recursive_mutex.
        m_writeMutex.lock();
        ExecOrThrow(m_write, "BEGIN;");
    }

    void LocalIndex::EndTransaction(bool commit)
    {
        char* errMsg = nullptr;
        sqlite3_exec(m_write, commit ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, &errMsg);
        sqlite3_free(errMsg);
        m_writeMutex.unlock();
    }
}
