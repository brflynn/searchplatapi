# Windows Search Platform APIs
Public Wrappers for Common Windows Search Service Platform APIs

These APIs assist developers in programming against the Windows Search Service in a much easier fashion than today. APIs are pretty verbose, and spread across many different header files. This takes the most commonly used functionality and puts it in a simple to use header file for Win32 application developers.

## CI/CD Workflows

This repository includes two GitHub Actions workflows:

### Pull Request Workflow
- **Trigger**: Pull requests to the `main` branch
- **Purpose**: Builds the solution and runs tests to ensure code quality
- **Environment**: Windows Server 2022 with Visual Studio 2022, matching the
  projects' v143 toolset and WinUI/UWP build targets. MSBuild and VSTest are
  selected from that Visual Studio installation rather than a moving
  `windows-latest` image.
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
  scanned, current path, elapsed time). SearchApp displays that live worker
  status in a left-side card and persisted totals (all items, files, folders,
  content-indexed files, and completed volumes) in a right-side card; both
  refresh once per second without blocking search.

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
theme, centered search box) are preserved. The window starts maximized but
uses the standard resizable/minimizable/maximizable presenter with
high-contrast caption buttons. At widths below 1240 effective pixels the
side status cards collapse and the result list/search box use the available
width. Ctrl+Shift+F is registered globally: it hides the app when the app is
foreground, or restores, clears, and focuses search otherwise.

### Index controls, exclusions, and Windows Search content scopes

**Reindex local volumes** in the background-index side card queues a new walk
of every fixed NTFS volume without deleting `index.db` or joining workers on
the UI thread. The same control and a dedicated status line are available
when the side cards collapse. Requests are coalesced into queued passes on the existing
scanner; a request during a scan schedules another pass. The card shows queued,
running, completed, or error/incomplete status. Completion refers to the name
scan; queued local content processing continues separately. Inaccessible
directories or enumeration failures prevent stale-row sweeping for that
volume, retaining previously indexed data instead of deleting unseen rows.

Each result's **... / Result actions** menu offers **Exclude folder** (the
containing folder for a file, or the folder itself) and **Exclude extension**.
These are visibility rules applied to both Windows Search and local FTS5
results, not indexer-scope exclusions. They retain filename/content data.
Folder rules use normalized absolute Windows paths, case-insensitive matching,
and exact-or-descendant boundaries (`C:\foo` does not exclude `C:\foobar`).
Extensions are canonicalized to lowercase `.ext`; folders, extensionless
files, dotfiles with no suffix, and trailing-dot names have no extension action.
Filtered hits do not consume the displayed result cap. Changing a rule
invalidates in-flight queries and refreshes the current search.

**Index settings**, available in both layouts, lists exclusions with **Restore**
buttons. Its toolbar sits below the custom title-bar drag region so settings
and compact reindex controls receive mouse clicks normally.
Restoring a rule brings retained results back without reindexing.
Settings use additive `exclusions` and `content_scopes` tables in the same
`%LOCALAPPDATA%\SearchApp\index.db`, with an independent SQLite connection so
settings operations do not hold the scanner's transaction lock.
Opening and migrating the local database runs in the background, so large
existing indexes cannot freeze the launch UI. Search waits for visibility
settings to load before displaying results. The scan-generation sequence is
seeded from existing rows only once; later launches read its single persisted
record without rescanning the entire file table.

**Request Windows Search content indexing for folder** is a separate, explicit
per-result action. It uses `ISearchCrawlScopeManager::AddUserScopeRule` to
include the folder URL, without overriding child rules, and `SaveAll` to
persist it. Already-included folders keep their existing scope rules.
URL generation escapes spaces, percent signs, reserved characters, and UTF-8
characters, and supports UNC paths. The request runs on an MTA background
thread; `IRowsetPrioritization::SetScopePriority` uses the SDK's
`PRIORITY_LEVEL_LOW` (background, not foreground; there is no
`PRIORITY_LEVEL_BACKGROUND` constant).

Scope requests and pending/success/failure details are persisted and listed in
settings. Failed, cancelled, or interrupted pending requests can be retried.
**Forget app record** only removes the app's tracking record; it never removes
Windows Search scope rules or undoes pre-existing system configuration.
Permission/COM failures are shown explicitly; the app never silently elevates
or changes scope on startup. Adding an inclusion can require administrator
assistance through Windows Indexing Options.

Catalog monitoring uses `ISearchCatalogManager::GetCatalogStatus`, checks every
two seconds, stops after two minutes, and cancels on window close. Scope
inclusion and `CATALOG_STATUS_IDLE` are **not proof that all content in the
folder was indexed**: idle describes the entire catalog, while installed
IFilters, file-type content settings, excluded child rules, service policies,
and permissions determine actual coverage. A timeout/cancellation does not
stop Windows Search's independent indexing.

### Tests

`src/test` includes unit tests for the new functionality:
`LocalIndexTests.cpp` (SQLite/FTS5 schema, filename/content search,
incremental mtime-skip, scan-generation deletion sweep, transactions),
`BackgroundIndexerTests.cpp` (content extension allow-list, binary-file
sniffing), and `MatchKindClassifierTests.cpp` (rank-tier and metadata-match
classification logic). After building the x64 Debug app, run
`src\test\SearchAppUiSmokeTests.ps1` locally in an interactive desktop
session to exercise the real search box, UI-thread responsiveness, narrow/
wide resize behavior, and the global Ctrl+Shift+F hotkey.
The smoke test creates a temporary fixture and sets `SEARCHAPP_UI_SMOKE_ROOT`
only for its child app: this switches local scanning/storage to that fixture
and disables Windows Search scope mutations. It exercises reindex, empty
settings, both exclusion actions, and restoring retained results in the real
window without changing the user's local database or Windows Search scopes.
Additional C++ tests cover settings reopen/undo, duplicate normalization,
folder boundaries/case, extension handling, before-cap filename/content
filtering, escaped folder URLs, queued rescans, and incomplete-scan retention.
