# Memory Service

A high-performance hierarchical memory store for AI agents, providing semantic search across conversation history with sub-10ms query latency.

## Overview

Memory Service stores agent conversations in a 4-level hierarchy:

```
session          <- Agent work session (e.g., "auth-system")
    └── message      <- Single message/turn in conversation
        └── block        <- Logical section (code, explanation, etc.)
            └── statement    <- Individual sentence or line
```

Each level is independently indexed for semantic search, allowing agents to:
- Find relevant context at any granularity
- Drill down from high-level topics to specific details
- Zoom out from details to broader context

## Features

- **Hierarchical Storage**: 4-level tree structure (Session → Message → Block → Statement)
- **Semantic Search**: ONNX Runtime embeddings with HNSW indexing
- **Level Filtering**: Search specific levels or ranges
- **Text Filtering**: Filter drill-down results by content
- **Cross-Platform**: Builds on Linux (Docker) and macOS (native with CoreML)
- **JSON-RPC 2.0**: Standard API over HTTP

## Building

### Prerequisites

**Required:**
- GCC or Clang (C11)
- pthreads

**Optional (auto-detected):**
- LMDB - metadata storage
- libmicrohttpd - HTTP server
- ONNX Runtime - embedding inference

### Build Commands

```bash
make              # Release build
make debug        # Debug build with sanitizers
make test         # Run all tests
make vars         # Show detected configuration
make clean        # Clean build artifacts
```

### Platform Detection

The Makefile auto-detects:
- Platform (Linux/macOS)
- Architecture (x86_64/arm64)
- Dependencies (LMDB, libmicrohttpd, ONNX Runtime)

```bash
# Check what was detected
make vars

# Override ONNX Runtime location if needed
make ONNXRUNTIME_ROOT=/path/to/onnxruntime
```

### Docker

```bash
docker compose up --build
```

Builds for Linux/aarch64 with CPU inference.

### Native macOS (Apple Silicon)

```bash
brew install lmdb libmicrohttpd onnxruntime
make
```

Builds with CoreML acceleration for faster inference.

## Running

### Start the Service

```bash
# Default: listens on port 8080
./build/bin/memory-service

# Custom port
./build/bin/memory-service --port 9000

# With debug logging
LOG_LEVEL=debug ./build/bin/memory-service

# JSON log format (for production)
LOG_FORMAT=json ./build/bin/memory-service
```

### Docker

```bash
# Start service
docker compose up -d

# View logs
docker compose logs -f

# Stop service
docker compose down
```

### Verify Service is Running

```bash
# Health check
curl http://localhost:8080/health

# Or use the test scripts (will error if service is down)
./tests/qa/sessions.sh
```

### Claude Code Integration

The memory service stores messages, but they must be actively pushed from Claude Code. Configure hooks to automatically ship conversation data:

Copy the `../memory-push.sh` hook script to your Claude hooks directory and configure your `~/.claude.json`:

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/memory-push.sh",
            "timeout": 5
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/memory-push.sh",
            "timeout": 5
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/memory-push.sh",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

**Hook Events:**
| Event | When Triggered |
|-------|----------------|
| `UserPromptSubmit` | User sends a message |
| `Stop` | Claude finishes responding |
| `PostToolUse` | After any tool is executed |

## ONNX Model Setup

The service uses ONNX Runtime for generating text embeddings. Without a model, it falls back to stub embeddings (random vectors) which won't provide meaningful semantic search.

### Download a Pre-trained Model

We recommend `all-MiniLM-L6-v2` from Sentence Transformers - it's small (80MB), fast, and produces good embeddings.

```bash
# Download default model (all-MiniLM-L6-v2)
./scripts/download-model.sh

# Or specify a different model
./scripts/download-model.sh all-mpnet-base-v2
```

This downloads the ONNX model directly from Hugging Face using curl (no Python required).

**Manual download:**

```bash
mkdir -p models/all-MiniLM-L6-v2
curl -L -o models/all-MiniLM-L6-v2/model.onnx \
  "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx"
```

### Run with Model

```bash
./build/bin/memory-service --model ./models/all-MiniLM-L6-v2/model.onnx
```

### Verify Model is Loaded

When the model loads successfully, you'll see:
```
INFO  ONNX model loaded: ./models/all-MiniLM-L6-v2/model.onnx
INFO  Using ONNX execution provider: CoreML  # or CPU on Linux
```

Instead of:
```
WARN  No ONNX model path provided - using stub embeddings
```

### Recommended Models from Hugging Face

All models are from [Sentence Transformers](https://huggingface.co/sentence-transformers) on Hugging Face. Download any of them with:

```bash
./scripts/download-model.sh <model-name>
```

#### General Purpose (Recommended)

| Model | Size | Dim | Speed | Quality | Use Case |
|-------|------|-----|-------|---------|----------|
| [all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2) | 80MB | 384 | ⚡ Fast | Good | **Default choice** - best speed/quality balance |
| [all-MiniLM-L12-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L12-v2) | 120MB | 384 | Medium | Better | More accuracy, still reasonable speed |
| [all-mpnet-base-v2](https://huggingface.co/sentence-transformers/all-mpnet-base-v2) | 420MB | 768 | Slow | Best | Highest quality, for accuracy-critical use |

#### Multilingual

| Model | Size | Dim | Languages | Use Case |
|-------|------|-----|-----------|----------|
| [paraphrase-multilingual-MiniLM-L12-v2](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2) | 420MB | 384 | 50+ | Multi-language support |
| [distiluse-base-multilingual-cased-v2](https://huggingface.co/sentence-transformers/distiluse-base-multilingual-cased-v2) | 480MB | 512 | 50+ | Better multilingual quality |

#### Code & Technical

| Model | Size | Dim | Use Case |
|-------|------|-----|----------|
| [all-roberta-large-v1](https://huggingface.co/sentence-transformers/all-roberta-large-v1) | 1.3GB | 1024 | Technical/code documentation |
| [multi-qa-mpnet-base-dot-v1](https://huggingface.co/sentence-transformers/multi-qa-mpnet-base-dot-v1) | 420MB | 768 | Question-answering, search |

#### Lightweight (Edge/Mobile)

| Model | Size | Dim | Use Case |
|-------|------|-----|----------|
| [all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2) | 80MB | 384 | Resource-constrained environments |
| [paraphrase-MiniLM-L3-v2](https://huggingface.co/sentence-transformers/paraphrase-MiniLM-L3-v2) | 60MB | 384 | Smallest, fastest |

### Choosing a Model

- **Local development**: `all-MiniLM-L6-v2` (fast iteration)
- **Production**: `all-mpnet-base-v2` (best quality)
- **Multi-language**: `paraphrase-multilingual-MiniLM-L12-v2`
- **Code/technical docs**: `multi-qa-mpnet-base-dot-v1`
- **Edge deployment**: `paraphrase-MiniLM-L3-v2`

### Browse More Models

Explore all available models at:
- [Sentence Transformers Models](https://huggingface.co/models?library=sentence-transformers&sort=downloads)
- [MTEB Leaderboard](https://huggingface.co/spaces/mteb/leaderboard) - benchmark comparisons

Filter by task type (semantic search, clustering, etc.) and sort by downloads to find popular, well-tested models.

## API Reference

### Store Methods

#### `store` - Store a message
```json
{"jsonrpc": "2.0", "method": "store", "params": {
  "session_id": "auth-system",
  "agent_id": "coding-agent",
  "content": "Implementing JWT authentication"
}, "id": 1}
```

#### `store_block` - Store a block under a message
```json
{"jsonrpc": "2.0", "method": "store_block", "params": {
  "parent_id": 5,
  "content": "function generateToken(userId) { ... }"
}, "id": 1}
```

#### `store_statement` - Store a statement under a block
```json
{"jsonrpc": "2.0", "method": "store_statement", "params": {
  "parent_id": 10,
  "content": "Import the jsonwebtoken library"
}, "id": 1}
```

### Search Methods

#### `query` - Semantic search with level filtering
```json
{"jsonrpc": "2.0", "method": "query", "params": {
  "query": "authentication token",
  "level": "block",
  "max_results": 10
}, "id": 1}
```

**Parameters:**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `query` | string | required | Search text |
| `max_results` | int | 10 | Max results (≤100) |
| `level` | string | - | Single level to search |
| `top_level` | string | "session" | Highest level to search |
| `bottom_level` | string | "statement" | Lowest level to search |

**Level hierarchy (top to bottom):**
```
session -> message -> block -> statement
```

### Navigation Methods

#### `drill_down` - Get children with optional filter
```json
{"jsonrpc": "2.0", "method": "drill_down", "params": {
  "id": 5,
  "filter": "token",
  "max_results": 20
}, "id": 1}
```

**Parameters:**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | int | required | Node ID to drill from |
| `filter` | string | - | Case-insensitive text filter |
| `max_results` | int | 100 | Max children returned |

#### `zoom_out` - Get ancestor chain
```json
{"jsonrpc": "2.0", "method": "zoom_out", "params": {
  "id": 15
}, "id": 1}
```

### Session Methods

#### `list_sessions` - List all sessions
```json
{"jsonrpc": "2.0", "method": "list_sessions", "params": {}, "id": 1}
```

#### `get_session` - Get session details
```json
{"jsonrpc": "2.0", "method": "get_session", "params": {
  "session_id": "auth-system"
}, "id": 1}
```

## Usage Examples

Test scripts are in `tests/qa/`:

```bash
# Populate test data
./tests/qa/mkdb.sh

# Search with level filtering
./tests/qa/search.sh "authentication"                # All levels
./tests/qa/search.sh "database" --level block        # Blocks only
./tests/qa/search.sh "JWT" --top message             # Skip session level
./tests/qa/search.sh "API" --bottom block            # Skip statements

# Drill down with text filter
./tests/qa/drill_down.sh 0                           # All children of node 0
./tests/qa/drill_down.sh 5 --filter "token"          # Filter by text
./tests/qa/drill_down.sh 2 --max 10                  # Limit results

# Zoom out to see context
./tests/qa/zoom_out.sh 15                            # Get ancestors of node 15

# Store content
./tests/qa/store.sh message "my-session" "agent" "Working on feature X"
./tests/qa/store.sh block 5 "Here is the implementation"
./tests/qa/store.sh statement 10 "Import required modules"

# Session management
./tests/qa/sessions.sh                               # List all sessions
./tests/qa/sessions.sh auth-system                   # Get specific session
```

## Architecture

```
src/
├── api/           # HTTP server, JSON-RPC handlers
├── core/          # Arena allocator, hierarchy tree
├── embedding/     # ONNX Runtime, tokenizer, pooling
├── events/        # Event emitter
├── platform/      # Platform-specific code (Linux/macOS)
├── search/        # HNSW index, inverted index, ranking
├── session/       # Session management, keywords
├── storage/       # Embeddings, metadata, WAL
└── util/          # Logging, time utilities
```

### Platform Abstraction

Platform-specific code is isolated in `src/platform/`:
- `platform_linux.c` - Uses `mremap()` for memory remapping
- `platform_darwin.c` - Uses `munmap()+mmap()` fallback, CoreML provider

### Search Ranking

Results are ranked by:
```
score = 0.6 * relevance + 0.3 * recency + 0.1 * level_boost
```

Where:
- `relevance` = semantic similarity + exact match score
- `recency` = exponential decay (1-hour half-life)
- `level_boost` = slight preference for higher levels

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMORY_SERVICE_HOST` | `localhost:8080` | Service endpoint |
| `MEMORY_SERVICE_PORT` | `8080` | HTTP port |
| `MEMORY_SERVICE_DATA` | `./data` | Data directory |
| `LOG_LEVEL` | `info` | Log level (debug/info/warn/error) |
| `LOG_FORMAT` | `text` | Log format (text/json) |

## License

See LICENSE file.
