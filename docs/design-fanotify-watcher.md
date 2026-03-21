# Design: fandex as a Filesystem Index

## Vision

fandex evolves from a document store into a **search and indexing layer over the filesystem**. Files on disk are the source of truth. fandex watches `~/projects` via fanotify and maintains a searchable index of everything in it.

### The key insight

The agent's project folder **is** the indexed volume. There's no separate "memory store." The project itself is the memory. Agents and humans work with files using any tool they already have. fandex just makes it all searchable.

### Separation of concerns

| System | Responsibility |
|---|---|
| Filesystem | Content storage (source of truth) |
| Git | History, versioning, collaboration |
| fandex | Search and indexing (current state only) |

fandex does not store content, does not track history, and does not manage versions. If the SQLite database is deleted, it rebuilds the entire index from the current filesystem on next startup. The database is a **cache**, not a source of truth.

### What fandex becomes

| Before | After |
|---|---|
| Document store (body lives in SQLite) | Search index (content lives on disk) |
| Create/update via HTTP API | Create/update by writing files |
| API is the only way in | Filesystem is the primary interface |
| Full CRUD service | Search + reference service |
| Revision history and soft deletes | None — git handles history |

### What goes away from the current codebase

| Current feature | Still needed? | Why |
|---|---|---|
| `revisions` table | No | Git history |
| `deleted_at` / soft deletes | No | File is gone → remove from index |
| `snapshotRevision()` | No | Git commits |
| `PUT /documents` | No | Edit the file, fanotify re-indexes |
| `POST /documents` | No | Create a file |
| `DELETE /documents` | No | Delete the file |
| Write API (all of it) | No | Filesystem is the write interface |

## Workspace Layout

The watched directory is `~/projects`. Every project the user works on is automatically indexed.

```
~/projects/
  fandex/        ← active project
  some-client-project/    ← active project
  new-experiment/         ← active project
  archive/
    old-project-a/        ← still indexed, still searchable
    old-project-b/        ← still indexed, still searchable
```

### Project lifecycle

- **Start working on a project**: clone it into `~/projects/`. fanotify detects the new files, indexes them.
- **Stop working on a project**: move it to `~/projects/archive/`. Still indexed, still searchable, just out of the way.
- **Done with a project entirely**: remove it from `~/projects/`. The index drops it. Git history lives on in the remote/mirror.
- **Need it again later**: clone it back. fanotify picks it up, fully searchable within seconds.

### Scoping is just path filtering

No metadata-based scoping needed. Project scoping maps directly to directory structure:

- Search within a project: filter by path prefix `~/projects/fandex/`
- Search active projects only: exclude `~/projects/archive/`
- Search everything: no filter

### Forever history

The git remote is the forever archive. `~/projects` is the working set. As long as git repos are never lost (mirrors, backup service, etc.), any project can be restored to the searchable workspace at any time.

### Storage: btrfs subvolume

`~/projects` is a **btrfs subvolume** backed by `/mnt/store/`. This gives it its own mount point, which is what fanotify needs to watch it. It also provides:

- **Snapshots** — instant, cheap snapshots of the entire projects volume
- **Compression** — transparent zstd compression on text-heavy project files
- **Checksumming** — silent data corruption detection

All agents and users access projects through `~/projects` — not through the underlying `/mnt/store/` path. This ensures all file events are captured by the fanotify listener on the `~/projects` mount.

## fanotify for Change Detection

Use Linux **fanotify** to detect filesystem changes in real time.

### Trade-offs

| Constraint | Accepted? | Notes |
|---|---|---|
| Requires `CAP_SYS_ADMIN` | Yes | Granted via file capabilities, not full root (see below) |
| Single mount point scope | Yes | `~/projects` resides on a single mount |
| Linux-only | Yes | All deployments target Linux |
| Kernel 5.1+ required | Yes | Needed for `FAN_CREATE` / `FAN_MODIFY` / `FAN_DELETE` events |

### Privilege Model

The binary runs as a **normal unprivileged user** with only `CAP_SYS_ADMIN` granted via Linux file capabilities:

```bash
sudo setcap cap_sys_admin+ep /path/to/fandex
```

- No root. No setuid. No systemd dependency.
- The capability is stored in filesystem extended attributes and survives reboots.
- Rebuilding the binary clears the capability — re-run `setcap` after each build.
- The `make install` target will include the `setcap` step.

### Why fanotify over alternatives

- **inotify** requires one watch per directory. At tens of thousands of directories, this hits kernel limits and requires manual management when new subdirectories appear.
- **Polling with find** is O(n) over all files on every cycle. Unacceptable at scale.
- **fanotify** monitors an entire mount point with a single file descriptor. No per-directory watches, no limits on file count, no need to handle new subdirectories — they are automatically covered.

### Events of Interest

| Event | Purpose |
|---|---|
| `FAN_CREATE` | New file detected — read and index it |
| `FAN_CLOSE_WRITE` | File finished being written — re-index it |
| `FAN_DELETE` | File removed — remove from index |
| `FAN_MOVED_FROM` / `FAN_MOVED_TO` | File renamed/moved — update path in index |

`FAN_CLOSE_WRITE` is preferred over `FAN_MODIFY` for triggering re-indexing, since `FAN_MODIFY` fires on every write syscall while `FAN_CLOSE_WRITE` fires once when the writer is done.

### .gitignore Filtering

fanotify watches the entire mount, but not all files should be indexed. fandex honors `.gitignore` files to filter events:

- `.git/` directories are always excluded (noisy — every commit triggers a storm of object file changes)
- `.gitignore` patterns at any level in the tree are respected (hierarchical, same semantics as git)
- A Go `.gitignore` parsing library handles the full spec (negation patterns, directory-only patterns, etc.)
- When fanotify detects a `.gitignore` file itself being modified, the exclusion rules are reloaded

This means projects define their own noise filters. Adding `*.log` to `.gitignore` drops log files from the index automatically.

## Architecture

```
 ~/projects (source of truth)
        │
        │  fanotify events
        ▼
┌──────────────────────────┐
│   fanotify listener      │
│                          │
│  - resolve file path     │
│  - check .gitignore      │
│  - debounce rapid writes │
└──────────┬───────────────┘
           │
           │  internal channel
           ▼
┌──────────────────────────┐
│   indexer                │
│                          │
│  - read file content     │
│  - chunk text            │
│  - update FTS5 index     │  ← fast (milliseconds)
│  - call embedding API    │  ← async (queued)
│  - update vector index   │
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│   SQLite                 │
│                          │
│  files     (metadata)    │
│  chunks    (chunk text)  │
│  chunks_fts (FTS5, contentless) │
│  chunks_vec (sqlite-vec) │
└──────────────────────────┘
           │
           │  HTTP API (search → path:line references)
           ▼
      agents / tools
```

## Full-Text Search: SQLite FTS5 Contentless

### Decision

Use SQLite FTS5 with `content=''` (contentless mode).

```sql
CREATE VIRTUAL TABLE chunks_fts USING fts5(body, content='');
```

### Why contentless

In normal mode, FTS5 stores both the inverted index and a full copy of the original text. With `content=''`, it stores **only the inverted index** — a compact mapping from tokens to row IDs. The file content exists in exactly one place: the filesystem.

| Storage layer | What it holds | Relative size |
|---|---|---|
| Filesystem | File content (source of truth) | 100% |
| FTS5 contentless | Inverted index (token → row mappings) | ~10-30% of content |
| SQLite metadata table | Paths, mtime, hash, scoping | Negligible |

### Why not something else

- **FTS5 normal mode** — duplicates all file content in SQLite. Wasteful when files are already on disk.
- **Bleve** — pure Go but larger index size (~200% of content), slower indexing, more memory. Overkill.
- **Tantivy** — fastest option but requires CGo or a sidecar process. Unnecessary complexity.
- **External services** (Elasticsearch, Meilisearch) — operational overhead of a separate service. Defeats the nano-service model.

FTS5 contentless gives us:
- BM25 ranking (built in)
- Fast queries (~1-5ms on 500K docs)
- No content duplication
- Single SQLite file, no external dependencies
- Already integrated in the codebase

### Trade-offs accepted

- No `highlight()` or `snippet()` — these require stored content. The agent reads the file from disk.
- Manual index management — inserts and deletes must be explicitly issued. This is fine since the fanotify indexer manages all writes.

## Semantic Search: sqlite-vec

### Decision

Use **sqlite-vec** for vector similarity search. Files are chunked, embedded via LLM, and stored in SQLite alongside the FTS5 index.

### Why sqlite-vec

- Stays in the same SQLite database — one file for all index data
- No external service (Pinecone, Qdrant) to deploy and manage
- No CGo — sqlite-vec is a loadable SQLite extension, pure C
- Brute-force KNN with exact results (no approximate indexing)

### Performance envelope

| Vector count | ~Query time (1536-dim) | Verdict |
|---|---|---|
| 100K | ~10-30ms | Fine |
| 500K | ~50-100ms | Fine |
| 1M | ~100-200ms | Acceptable |
| 5M+ | ~500ms+ | Too slow |

At hundreds of thousands of files with 5-10 chunks each, we expect 500K-1M vectors. This is within sqlite-vec's comfortable range, especially since not every query needs vector search — FTS5 keyword search handles many lookups at <5ms.

### Escape hatch

If vector count exceeds ~2M or latency requirements tighten, **Qdrant** (self-hosted, single Rust binary) is the migration path. The chunks table and embedding logic stay the same — only the vector query backend changes.

### Chunking

Files are split into chunks for embedding. Chunk text is stored in a regular SQLite table (needed for re-embedding on content change and for mapping matches back to line numbers). Chunks are an internal implementation detail — agents never see them.

#### Pluggable chunker interface

Each file type can have its own chunking strategy. The indexer selects a chunker by file extension, falling back to the default.

```go
type Chunker interface {
    Chunk(content []byte) []Chunk
}

type Chunk struct {
    LineStart int
    LineEnd   int
    Body      string
}
```

```
indexer receives file path
    │
    ▼
lookup chunker by extension
    .md  → MarkdownChunker (splits on headings)
    .go  → TreeSitterChunker (splits on func/type declarations)
    .c   → TreeSitterChunker (splits on function definitions)
    .py  → TreeSitterChunker (splits on def/class)
    *    → DefaultChunker (line-based, see below)
    │
    ▼
chunker returns []Chunk with line numbers
```

Adding support for a new file type means implementing the `Chunker` interface — or, for code files, just defining which tree-sitter AST node types are chunk boundaries (a few lines of config).

#### Default chunker (fallback)

The default chunker requires zero knowledge of document structure. It works on anything — code, prose, config files, whatever.

Algorithm:

1. Split file into lines
2. Accumulate lines until reaching the target chunk size
3. Never split mid-line
4. When starting a new chunk, overlap by N lines from the previous chunk
5. Repeat until end of file

```
file.txt (20 lines, target=10 lines, overlap=3 lines)

chunk 0:  lines  1-10
chunk 1:  lines  8-17  (overlaps 3 lines: 8, 9, 10)
chunk 2:  lines 15-20  (overlaps 3 lines: 15, 16, 17)
```

The overlap ensures a search hit near a chunk boundary still has surrounding context in at least one chunk. Splitting on newlines means we never break a sentence, a code statement, or a heading in half.

Configurable parameters:
- **Target chunk size** — in lines or bytes
- **Overlap** — number of lines repeated from the previous chunk

This is the worst-performing strategy (no structural awareness) but it's always available and works on every file type. Structured chunkers (markdown headings, tree-sitter for code) override it for file types they understand.

### Schema

```sql
-- file metadata
CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    mtime TEXT,
    size INTEGER,
    hash TEXT,
    indexed_at TEXT
);

-- chunks (internal — agents never see these directly)
CREATE TABLE chunks (
    id INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id),
    chunk_index INTEGER NOT NULL,
    line_start INTEGER NOT NULL,
    line_end INTEGER NOT NULL,
    body TEXT NOT NULL
);

-- keyword search (contentless — no text duplication for FTS)
CREATE VIRTUAL TABLE chunks_fts USING fts5(body, content='');

-- vector search
CREATE VIRTUAL TABLE chunks_vec USING vec0(
    embedding float[1536]
);
```

### Dual search paths

Both FTS5 and sqlite-vec return chunk IDs, which resolve to files and line numbers.

```
keyword search:  chunks_fts MATCH 'query'  →  chunk  →  file path + line number
semantic search: vec_distance_cosine(...)   →  chunk  →  file path + line number
```

Chunks are internal. The API collapses results to file-level references. The agent gets `path:line` and uses its own file read tools to access the content with full document context.

### Split indexing pipeline

FTS5 indexing and vector embedding are split into two stages:

1. **Immediate** — FTS5 + chunk table updates happen synchronously on file change. This is fast (milliseconds). Line number mappings are fresh almost instantly.
2. **Async** — Embedding API calls are queued and processed asynchronously. Semantic search results may lag slightly behind file changes.

This means keyword search line numbers are almost never stale. Semantic search catches up shortly after.

### Stale reference mitigation

Between a file modification and re-indexing, line numbers from the old index could point to the wrong place. Mitigations:

- **Content anchors** — each search result includes a short text anchor (first few words of the matching chunk). If the line number is stale, the agent can grep the file for the anchor text.
- **Split pipeline** — FTS5 re-indexing is fast (milliseconds), so the staleness window for keyword search is near-zero. Only semantic search may lag.

```json
{"path": "~/projects/fandex/docs/design.md", "line": 142, "anchor": "Use sqlite-vec for vector similarity", "score": 0.87}
```

### Embedding cost consideration

Re-embedding on every file change has API cost. Mitigations:
- Content hash comparison — skip re-embedding if the chunk text hasn't changed
- Batch embedding API calls
- Only embed text files (skip binaries, configs, lockfiles via .gitignore)
- Rate-limit embedding during bulk initial sync

## Transport: JSON-RPC 2.0 over Unix Socket

### Decision

The API uses **JSON-RPC 2.0** over a **Unix domain socket** with newline-delimited messages (one JSON object per line).

### Why Unix socket

- The watched volume is always local filesystem — the agent must be on the same machine
- No network exposure — can't be reached from outside the machine
- No port conflicts — it's just a file
- Slightly faster than TCP loopback (no TCP overhead)
- File permissions control access — standard Unix security model

Socket path: `~/.local/state/ralph/fandex.sock`

### Why JSON-RPC 2.0

- Standard spec — libraries exist in every language
- Minimal overhead — 22 bytes per message (`"jsonrpc":"2.0"` + `"id":1`)
- Request/response correlation via `id` — supports concurrent requests on one connection
- Standardized error format
- Dead simple to debug: `socat` to the socket, type JSON, get JSON back

### Wire format

One JSON object per line. Newline (`\n`) is the message delimiter.

```
→ {"jsonrpc":"2.0","id":1,"method":"remember_search","params":{"query":"fanotify"}}\n
← {"jsonrpc":"2.0","id":1,"result":[{"path":"~/projects/fandex/docs/design.md","line":142,"anchor":"Use sqlite-vec for vector","score":0.87}]}\n
```

### Why not HTTP

- HTTP adds framing complexity (content-length, headers, chunked encoding) for no benefit on a local socket
- JSON-RPC over a raw socket is the simplest possible protocol: write a line, read a line
- No HTTP library dependency — any language that can open a Unix socket and write JSON can be a client

## Agent Tool Interface

### Design principle

fandex returns **references, not content**. Search results are `path:line` pointers with a text anchor for resilience. The agent uses its existing file read tools to access the document with full context intact.

This means:
- No content in API responses — smaller payloads, less context window consumed
- No `read` tool needed — the agent already has one
- No `write` tool needed — the agent writes files directly, fanotify picks them up
- Chunks are invisible — they're an indexing implementation detail, not an API concept
- The agent always sees the real document, not a fragment with missing context

### Methods

#### `remember_search`

The primary method. Searches both keyword (FTS5) and semantic (sqlite-vec) indexes.

Request:
```json
{"jsonrpc":"2.0","id":1,"method":"remember_search","params":{"query":"fanotify","project":"fandex","limit":10}}
```

Response:
```json
{"jsonrpc":"2.0","id":1,"result":[
  {"path":"~/projects/fandex/docs/design.md","line":142,"anchor":"Use sqlite-vec for vector similarity","score":0.87},
  {"path":"~/projects/fandex/docs/design.md","line":58,"anchor":"fanotify monitors an entire mount point","score":0.73},
  {"path":"~/projects/other-project/notes/search.md","line":1,"anchor":"Notes on search implementation","score":0.65}
]}
```

Parameters:
- `query` (string, required) — natural language or keywords
- `project` (string, optional) — subdirectory name to scope search
- `limit` (integer, optional) — max results, default 10

#### `remember_list`

Browse indexed files without a search query.

Request:
```json
{"jsonrpc":"2.0","id":2,"method":"remember_list","params":{"project":"fandex","limit":20}}
```

Response:
```json
{"jsonrpc":"2.0","id":2,"result":[
  {"path":"~/projects/fandex/docs/design.md","size":4096,"modified":"2026-03-20T14:00:00Z"},
  {"path":"~/projects/fandex/docs/overview.md","size":2048,"modified":"2026-03-19T10:00:00Z"}
]}
```

Parameters:
- `project` (string, optional) — subdirectory name to scope listing
- `prefix` (string, optional) — path prefix filter
- `limit` (integer, optional) — default 20

#### `remember_status`

Index health check.

Request:
```json
{"jsonrpc":"2.0","id":3,"method":"remember_status"}
```

Response:
```json
{"jsonrpc":"2.0","id":3,"result":{"files_indexed":14285,"chunks_indexed":89142,"last_event":"2026-03-21T14:32:01Z","index_lag_seconds":0.3}}
```

### Error responses

Standard JSON-RPC 2.0 error format:

```json
{"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"method not found"}}
```

### What agents do NOT need

- **`remember_read`** — agents read files with their own tools. fandex points, it doesn't serve.
- **`remember_write`** — agents write files directly. fanotify detects and indexes the change.
- **`remember_delete`** — agents delete files directly. fanotify detects the removal and updates the index.

## Implementation: C with ikigai-1 Conventions

### Decision

fandex is a **rewrite in C**, following the conventions established in the ikigai-1 project. The current Go codebase is replaced — the new system shares almost nothing with it.

### Why C

- **Everything is native C** — SQLite, sqlite-vec, tree-sitter, fanotify syscalls, yyjson. No FFI, no bindings, no bridging layer.
- **Single static binary** — simple to build and distribute.
- **Fast compile times** — milliseconds, not minutes.
- **Minimal binary size** — smallest of all options.

### Namespace

All public symbols use the `fx_` prefix (fandex):

```c
fx_indexer_init()
fx_search_execute()
fx_chunker_default()
fx_fanotify_start()
fx_jsonrpc_handle()
```

### Conventions from ikigai-1

| Convention | How it applies |
|---|---|
| **talloc** hierarchical memory | Request context → allocate on it → free when done. fanotify event processing, JSON-RPC request/response lifecycles. |
| **res_t / TRY / CHECK** | fanotify errors, SQLite errors, socket I/O, file read failures. |
| **Wrapper functions** (weak symbols) | Wrap SQLite calls, fanotify syscalls, file I/O for testability and failure injection in tests. |
| **Poison header** | Ban unsafe string functions (`sprintf`, `strcpy`, etc.). Include last in every `.c` file. |
| **Sized integer types** | `int32_t`, `uint64_t`, `size_t` — never bare `int` or `long`. |
| **Include order** | Own header, project headers (alphabetical), system headers (alphabetical), poison last. |
| **16KB file size limit** | Split before it hurts. |
| **90% branch coverage** | Same quality gate as ikigai-1. |
| **Build modes** | debug, release, sanitize, tsan, valgrind, coverage — same Makefile structure. |
| **`//` comments only** | Comment why, not what. Use sparingly. |

### Libraries

| Library | Purpose |
|---|---|
| **talloc** | Hierarchical memory management |
| **SQLite** | Metadata storage, FTS5 full-text search |
| **sqlite-vec** | Vector similarity search (loadable extension) |
| **yyjson** | JSON parsing/generation (JSON-RPC messages) |
| **tree-sitter** | Code-aware chunking (per-language AST parsing) |

### Build and install

```bash
make                          # Debug build
make BUILD=release            # Release build
make install                  # Install binary + setcap for fanotify
```

The `make install` target includes the `setcap cap_sys_admin+ep` step so the binary can use fanotify without root.

### Concurrency model

The system is a pipeline of queues. Each stage is independent, communicates only through thread-safe queues (reusing the ikigai-1 queue implementation), and can be tested in isolation.

```
[fanotify fd] ─→ queue 1 ─→ [indexer] ─→ queue 2 ─→ [embedding workers] ─→ queue 3 ─→ [vector writer]

[unix socket] ─→ [jsonrpc] ─→ SQLite reads (independent, WAL mode)
```

#### Threads

| Thread(s) | Count | Role |
|---|---|---|
| fanotify | 1 | Read events from kernel, filter .gitignore, debounce, push file paths to queue 1 |
| indexer | 1 | Pop file paths from queue 1, read file, chunk, write FTS5 + metadata to SQLite, push chunks to queue 2 |
| embedding workers | N | Pop chunks from queue 2, call embedding API in parallel, push completed vectors to queue 3 |
| vector writer | 1 | Pop completed vectors from queue 3, write to sqlite-vec |
| jsonrpc | 1 | Accept connections on Unix socket, run search queries against SQLite |

#### Queues

| Queue | Producer | Consumer | Item |
|---|---|---|---|
| 1: file paths | fanotify thread | indexer thread | file path + event type |
| 2: chunks to embed | indexer thread | N embedding workers | chunk ID + text |
| 3: completed vectors | embedding workers | vector writer thread | chunk ID + embedding vector |

All three queues are the same data structure — mutex + condition variable + linked list. One implementation, used three times.

#### SQLite concurrency

SQLite runs in **WAL mode**:
- The indexer and vector writer are the only writers — they don't run concurrently (indexer writes FTS/metadata, vector writer writes embeddings, different stages of the pipeline)
- The jsonrpc thread reads concurrently without blocking or being blocked by writes

#### Why this works

- **Keyword search is fast** — FTS5 + metadata are written by the indexer immediately. Keyword search results are up to date within milliseconds of a file change.
- **Semantic search catches up** — embedding API calls are the slow path. Vectors trickle in as the API responds. A dozen file changes fire off a dozen parallel API calls.
- **No stage blocks another** — queues decouple everything. A slow embedding API doesn't stall fanotify event processing or query responses.

### Configuration

Compiled-in defaults for all settings. Runtime overrides stored in the SQLite database.

```bash
fandex config set watch_path=/home/user/projects
fandex config get watch_path
```

The binary works out of the box with zero configuration for the common case.

## Startup: Initial Sync

On startup, fandex reconciles the index with the current filesystem state:

1. Walk `~/projects`, respecting `.gitignore` files
2. Compare each file's mtime/hash against the index
3. Re-index changed or new files
4. Remove index entries for files that no longer exist on disk
5. Begin listening for fanotify events

This handles the case where files changed while the service was down. If the database doesn't exist, the full tree is indexed from scratch.

## Deployment: systemd User Service

fandex runs as a **systemd user service** — no root needed for the service itself (the binary has `CAP_SYS_ADMIN` via setcap).

### Service file

`~/.config/systemd/user/fandex.service`:

```ini
[Unit]
Description=fandex filesystem index
After=local-fs.target

[Service]
ExecStart=%h/.local/bin/fandex --watch %h/projects
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

### Setup

```bash
systemctl --user daemon-reload
systemctl --user enable fandex
systemctl --user start fandex
sudo loginctl enable-linger $USER          # run even when not logged in
```

### Logging

Traditional format to stdout. journald captures it automatically — no log files, no log rotation, no log directory to manage.

```
2026-03-21T14:32:01Z indexed ~/projects/fandex/docs/design.md chunks=7 12ms
2026-03-21T14:32:01Z search query="fanotify" results=3 5ms
2026-03-21T14:32:02Z fanotify CLOSE_WRITE ~/projects/fandex/main.c
```

```bash
journalctl --user -u fandex -f              # follow live
journalctl --user -u fandex --since "1h ago" # recent logs
journalctl --user -u fandex -p err           # errors only
```

### After rebuild

```bash
make install                                   # rebuild + setcap
systemctl --user restart fandex       # pick up new binary
```

## Module Structure

Each module lives in its own directory, exposes a single public header, and hides all implementation details. Tests only touch the public API.

### Source layout

```
src/
  queue/
    queue.h                ← public API
    queue.c

  db/
    db.h                   ← public API
    db.c

  config/
    config.h               ← public API
    config.c

  gitignore/
    gitignore.h            ← public API
    gitignore.c

  chunker/
    chunker.h              ← public API
    chunker.c
    chunker_default.c
    chunker_markdown.c

  fanotify/
    fanotify.h             ← public API
    fanotify.c

  indexer/
    indexer.h              ← public API
    indexer.c

  search/
    search.h               ← public API
    search.c

  jsonrpc/
    jsonrpc.h              ← public API
    jsonrpc.c

  embedding/
    embedding.h            ← public API
    embedding.c

tests/
  test_queue.c
  test_db.c
  test_config.c
  test_gitignore.c
  test_chunker.c
  test_fanotify.c
  test_indexer.c
  test_search.c
  test_jsonrpc.c
  test_embedding.c
```

### Module rules

- **One public header per module** — that's the API boundary. Tests include only this header.
- **Internal functions are not in the public header** — they're `static` or in an internal header that tests never include.
- **Dependencies are injected** — a module takes what it needs as arguments (`sqlite3 *`, function pointers, queue pointers). It doesn't reach out and grab globals.
- **Tests create the module, call its API, assert results** — no reaching into internals.

### Dependency injection by module

| Module | Injected dependencies |
|---|---|
| queue | None — standalone |
| db | SQLite path (`:memory:` in tests) |
| config | `sqlite3 *` |
| gitignore | Root path (temp dir in tests) |
| chunker | None — pure functions |
| fanotify | Output queue |
| indexer | `sqlite3 *`, `fx_chunker_t *`, `fx_embed_fn`, input queue, output queue |
| search | `sqlite3 *` |
| jsonrpc | `fx_search_t *`, socket fd |
| embedding | API endpoint + credentials (fake in tests) |

### Testing approach

**Unit tests only** — each test file exercises one module's public API in isolation. No cross-module composition in tests.

**Framework:** Check (same as ikigai-1).

**How each module is tested without wrappers:**

| Module | Test strategy |
|---|---|
| chunker | Pure functions — pass bytes, assert chunks |
| queue | Push/pop, assert ordering, test blocking behavior |
| db | In-memory SQLite — create schema, insert, query, assert |
| config | In-memory SQLite — set/get values, assert defaults |
| gitignore | Temp directory with `.gitignore` files — check paths against filter |
| indexer | In-memory SQLite + temp dir with test files + injected fake `fx_embed_fn` |
| search | In-memory SQLite pre-populated with known data — query, assert results |
| jsonrpc | Pass request JSON string, assert response JSON string |
| embedding | Injected function pointer — swap in fake that returns canned vectors |
| fanotify | Skip fanotify thread entirely — push events directly onto queue 1 |

**Quality gates** (same as ikigai-1):
- 90% branch coverage
- AddressSanitizer + UBSan (`make BUILD=sanitize`)
- ThreadSanitizer (`make BUILD=tsan`)
- Valgrind memcheck (`make BUILD=valgrind`)

## Open Questions

- **Binary files** — index metadata only, or skip entirely?
- **Debounce window** — how long to wait after `FAN_CLOSE_WRITE` before re-indexing? (handles save-rename-save patterns)
- **Content hashing** — hash the file to avoid re-indexing unchanged content? Cost vs benefit at scale.
- **Max file size** — should there be a limit on files indexed for FTS and embedding?
- **Default chunk size** — target line count and overlap for the default chunker.
- **Tree-sitter languages** — which languages to support first for structured chunking?
- **Embedding model** — which model and dimensionality? (1536-dim OpenAI, 1024-dim Claude, smaller/faster alternatives?)
- **Hybrid search** — how to combine FTS5 and vector results? Score fusion (RRF), separate endpoints, or caller's choice?
- **Embedding backpressure** — how to handle burst file changes without overwhelming the embedding API?
- **Migration path** — how do existing fandex documents get exported to files for the transition?
