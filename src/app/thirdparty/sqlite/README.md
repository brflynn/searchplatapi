# SQLite (vendored)

This directory contains the unmodified SQLite amalgamation, version
**3.46.1** (`sqlite-amalgamation-3460100`), downloaded from
<https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip>.

* `sqlite3.c` / `sqlite3.h` — the full SQLite engine in a single
  translation unit, compiled directly into `SearchApp.vcxproj` (and
  `src/test`'s test project) with FTS5 enabled.

SQLite is in the public domain; see <https://www.sqlite.org/copyright.html>.
No changes have been made to the vendored source. To update, replace both
files with a newer amalgamation release and bump the version noted above.

Build flags used for this vendor copy (set in the consuming `.vcxproj`,
not baked into the source): `SQLITE_ENABLE_FTS5`, `SQLITE_THREADSAFE=1`,
`SQLITE_DEFAULT_MEMSTATUS=0`, `SQLITE_OMIT_LOAD_EXTENSION`,
`_CRT_SECURE_NO_WARNINGS`.
