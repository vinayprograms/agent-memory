# Memory Service

A (allegedly) high-performance hierarchical memory store for AI agents. This repository contains two implementations:

| Implementation | Language |
|----------------|----------|
| [memory-c](./memory-c/) | C |
| [memory-go](./memory-go/) | Go |

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

- **Hierarchical Storage**: 4-level tree structure (Session -> Message -> Block -> Statement)
- **Semantic Search**: Embedding-based similarity search with HNSW indexing
- **Level Filtering**: Search specific levels or ranges
- **Navigation**: Drill down into children, zoom out to ancestors
- **JSON-RPC 2.0**: Standard API over HTTP
- **MCP Support**: Model Context Protocol integration (Go implementation)

## Implementations

### memory-c

C implementation with ONNX Runtime for embeddings. (Allegedly) Optimized for performance with CoreML acceleration on macOS.

```bash
cd memory-c
make
./build/bin/memory-service --model ./models/all-MiniLM-L6-v2/model.onnx
```

See [memory-c/README.md](./memory-c/README.md) for detailed documentation.

### memory-go

Go implementation using Pebble for storage and Hugot for embeddings. Includes MCP server and database inspector.

```bash
cd memory-go
make
./build/memory-server
```

**Binaries:**
- `memory-server` - HTTP server with JSON-RPC API
- `memory-mcp` - MCP (Model Context Protocol) server
- `memory-inspect` - Database inspection CLI

## API Overview

### Store Methods

| Method | Description |
|--------|-------------|
| `store` | Store a message under a session |
| `store_block` | Store a block under a message |
| `store_statement` | Store a statement under a block |

### Search Methods

| Method | Description |
|--------|-------------|
| `query` | Semantic search with optional level filtering |
| `drill_down` | Get children of a node with optional text filter |
| `zoom_out` | Get ancestor chain of a node |

### Session Methods

| Method | Description |
|--------|-------------|
| `list_sessions` | List all sessions |
| `get_session` | Get session details |

## License

GPL-2.0 - See [LICENSE](./LICENSE) for details.
