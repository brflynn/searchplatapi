# Windows Search Platform APIs
Public Wrappers for Common Windows Search Service Platform APIs

These APIs assist developers in programming against the Windows Search Service in a much easier fashion than today. APIs are pretty verbose, and spread across many different header files. This takes the most commonly used functionality and puts it in a simple to use header file for Win32 application developers.

## CI/CD Workflows

This repository includes two GitHub Actions workflows:

### Pull Request Workflow
- **Trigger**: Pull requests to the `main` branch
- **Purpose**: Builds the solution and runs tests to ensure code quality
- **Steps**:
  - Restores NuGet packages
  - Builds the solution using MSBuild (Release x64)
  - Runs unit tests with VSTest

### Release Workflow
- **Trigger**: Pushes to the `release` branch
- **Purpose**: Packages header files and publishes to NuGet
- **Steps**:
  - Packages all header files from `src/api/` into a NuGet package
  - Publishes to NuGet.org (requires `NUGET_API_KEY` secret)
  - Uploads package as a build artifact

## NuGet Package

The header files are published as a NuGet package for easy consumption in C++ projects. The package includes all API headers from the `src/api/` directory.

## SearchApp: Everything-style full-system search sample

`src/app` (`SearchApp`) is a WinUI3/C++/WinRT sample app built on the `src/api`
headers. Beyond the fast, indexer-backed search-as-you-type experience
(`wsearch::SearchAsYouTypeSession`), it now also builds and queries a
background, full-filesystem "true index" — similar in spirit to
[voidtools Everything](https://www.voidtools.com/) — so results aren't
limited to whatever the Windows Search indexer has chosen to scope
(Libraries, Desktop, Start Menu, etc.).

### Background indexing ("true index")

- A background-priority walker (`src/app/BackgroundIndexer.h`/`.cpp`) enumerates
  every local fixed NTFS volume with `FindFirstFileW`/`FindNextFileW`,
  recursively, on threads created at `THREAD_PRIORITY_IDLE` that yield
  generously between directories so it never competes with foreground work.
  Reparse points/symlinks are skipped by default (configurable) to avoid
  cycles.
- Discovered files/folders (full path, size, attributes, created/modified/
  accessed timestamps) are persisted to a SQLite database with an FTS5
  virtual table for filename search, stored at
  `%LOCALAPPDATA%\SearchApp\index.db` (`src/app/LocalIndex.h`/`.cpp`,
  `src/app/thirdparty/sqlite/` — vendored public-domain SQLite 3.46.1
  amalgamation, compiled directly into the app with
  `SQLITE_ENABLE_FTS5`).
- After a subtree's name index completes, a second background thread
  performs content indexing for a configurable allow-list of text-like
  extensions (`.txt`, `.md`, `.cs`, `.cpp`, `.h`, `.json`, `.xml`, `.log`,
  etc.), skipping files that sniff as binary (NUL byte in the first few KB)
  and truncating/chunking large files sanely. Content is stored in its own
  FTS5 table keyed by path, separate from the filename index.
- Indexing is incremental: each full rescan of a volume gets a monotonically
  increasing scan generation; files not re-observed during that generation's
  walk are swept (handles deletes/renames) without needing the USN journal.
  Per-volume scan status (`pending`/`in_progress`/`done`) is persisted so the
  app resumes rather than rescanning everything from zero on every launch.
  (USN journal / directory-change-notification incremental updates are a
  possible future enhancement, not implemented in v1.)
- The indexer exposes start/stop/pause and a progress snapshot (files
  scanned, current path, elapsed time).

### Unified query + match provenance

On every keystroke, `MainWindow` queries both the live Windows Search
indexer (fast path, unchanged) and the local SQLite name+content index,
then merges results by full path (de-duplicated, preferring the indexer's
richer metadata/thumbnail when both sources return the same file).

Each result now carries a `MatchKind` (`Filename`, `Metadata`, or `Content`)
indicating *why* it matched, shown as a small colored badge next to the
existing icon/thumbnail, filename, and path:

- **Filename** — the query matched the file/folder name. For indexer-backed
  results this is derived from `SearchQueryBuilder`'s existing
  ABSOLUTE/MINMAX `System.Search.Rank` tiering (filename matches rank
  ~900-990); for local-index results, it's an `files_fts` hit.
- **Metadata** — the query matched a property such as Title, Author,
  Keywords, or Comment (indexer-backed results only in v1; confirmed via a
  cheap case-insensitive substring check against those `PKEY_*` properties
  already exposed by `wsearch::SearchResult`).
- **Content** — the query matched inside the file's contents (indexer rank
  tier 0-899, or a `content_fts` hit from the local index).

All existing interactions (Enter to open, Escape to clear/close, arrow-down
to focus the list, double-tap to open) and the app's look and feel (dark
theme, centered search box, fullscreen results list) are unchanged.

### Tests

`src/test` includes unit tests for the new functionality:
`LocalIndexTests.cpp` (SQLite/FTS5 schema, filename/content search,
incremental mtime-skip, scan-generation deletion sweep, transactions),
`BackgroundIndexerTests.cpp` (content extension allow-list, binary-file
sniffing), and `MatchKindClassifierTests.cpp` (rank-tier and metadata-match
classification logic).
