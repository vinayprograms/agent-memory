# memory-go

Hierarchical memory store for AI agents.

## Overview

`memory-go` provides persistent memory for stateless AI coding agents. Instead of managing context windows, agents ship all messages to the memory service and query for relevant context based on current needs.

**Key Features:**
- Hierarchical data model (Session > Message > Block > Statement)
- Semantic search via HNSW vector index
- Hybrid persistence (PebbleDB + mmap)
- MCP server for Claude Code integration
- HTTP API for programmatic access

## Architecture

```
┌─────────────────┐     ┌─────────────────┐
│  memory-mcp     │────▶│  memory-server  │
│  (MCP client)   │     │  (HTTP service) │
└─────────────────┘     └────────┬────────┘
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
┌───────────────┐    ┌───────────────────┐    ┌──────────────────┐
│ Search Engine │    │ Hierarchy Manager │    │ Session Manager  │
│ (HNSW + BM25) │    │ (Tree structure)  │    │ (Metadata)       │
└───────┬───────┘    └─────────┬─────────┘    └────────┬─────────┘
        │                      │                       │
        └──────────────────────┼───────────────────────┘
                               ▼
                    ┌──────────────────┐
                    │    Storage       │
                    │   (PebbleDB)     │
                    └──────────────────┘
```

## Hierarchy Model

Data is stored as a tree where each node has vector embeddings and structural relationships:

| Level | Description | Example |
|-------|-------------|---------|
| Session | Entire conversation | Full agent work session |
| Message | Single turn | User request, assistant response |
| Block | Logical section | Code block, paragraph, tool output |
| Statement | Atomic unit | Individual sentence or code line |

## Installation

### Prerequisites

- Go 1.24+
- Make

### Build

```bash
# Build all binaries
make

# Or build individually
make build          # HTTP server
make build-mcp      # MCP server
make build-inspect  # Database inspector
```

Binaries are placed in `./build/`.

## Usage

### HTTP Server

```bash
# Start with defaults (port 8080, ./data directory, stub embeddings)
./build/memory-server

# Custom configuration
./build/memory-server -p 9090 -d /var/lib/memory --provider cpu

# With ONNX model for real embeddings
./build/memory-server --provider cpu -m /path/to/model.onnx
```

**Options:**
| Flag | Description | Default |
|------|-------------|---------|
| `-p, --port` | HTTP port | 8080 |
| `-d, --data-dir` | Data directory | ./data |
| `-m, --model` | ONNX model path | (stub) |
| `--provider` | Embedding provider (cpu, cuda, coreml, stub) | stub |
| `-l, --log-level` | Log level (trace, debug, info, warn, error) | info |

### MCP Server

The MCP server connects Claude Code to the memory service:

```bash
# Start HTTP server first
./build/memory-server -d ./data &

# Then run MCP server (connects to HTTP server)
./build/memory-mcp -u http://localhost:8080
```

**Claude Code Configuration** (`~/.claude.json`):

```json
{
  "mcpServers": {
    "memory": {
      "command": "/path/to/memory-mcp",
      "args": ["-u", "http://localhost:8080"]
    }
  }
}
```

### Claude Code Hooks

The MCP server enables *querying* memory, but messages must be actively *pushed* to the service. Configure Claude Code hooks to automatically ship conversation data:

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

### Database Inspector

```bash
./build/memory-inspect -d ./data
```

## API Reference

### HTTP Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Health check |
| GET | `/sessions` | List all sessions |
| GET | `/sessions/:id` | Get session details |
| GET | `/nodes/:id` | Get node by ID |
| POST | `/query` | Semantic search |
| POST | `/store` | Ingest content |
| POST | `/drill_down` | Get node children |
| POST | `/zoom_out` | Get node ancestors |
| POST | `/get_context` | Get node with context expansion |

### MCP Tools

| Tool | Description |
|------|-------------|
| `memory_query` | Semantic search across past interactions |
| `memory_drill_down` | Navigate to children of a node |
| `memory_zoom_out` | Get ancestor chain of a node |
| `memory_list_sessions` | List all conversation sessions |
| `memory_get_session` | Get session details and metadata |
| `memory_get_node` | Get full content of a node by ID |
| `memory_get_context` | Get node with optional context expansion |

### Query Example

```bash
curl -X POST http://localhost:8080/query \
  -H "Content-Type: application/json" \
  -d '{"query": "authentication flow", "max_results": 10}'
```

Response:
```json
{
  "results": [
    {
      "node_id": 12345,
      "level": "block",
      "content": "The authentication flow uses JWT tokens...",
      "relevance_score": 0.85,
      "recency_score": 0.92,
      "combined_score": 0.87
    }
  ],
  "total_results": 42,
  "truncated": false
}
```

## Docker

```bash
# Build image
make docker

# Run container
make docker-run

# Or manually
docker run -p 8080:8080 -v $(pwd)/data:/app/data memory-service:latest
```

## Development

```bash
# Run tests
make test

# Run with coverage
make test-coverage

# Format code
make fmt

# Lint (requires golangci-lint)
make lint

# Run in dev mode (stub embeddings, debug logging)
make run-dev
```

## Project Structure

```
memory-go/
├── cmd/
│   ├── memory-server/    # HTTP server entry point
│   ├── memory-mcp/       # MCP server entry point
│   └── memory-inspect/   # Database inspector CLI
├── internal/
│   ├── api/              # HTTP handlers
│   ├── core/             # Hierarchy manager
│   ├── embedding/        # ONNX and stub embedding engines
│   ├── events/           # Event emission
│   ├── parser/           # Content parsing
│   ├── search/           # HNSW and inverted index
│   ├── session/          # Session management
│   └── storage/          # PebbleDB persistence
├── pkg/
│   └── types/            # Public types and config
├── data/                 # Default data directory
├── notes/                # Design documentation
├── Makefile
├── Dockerfile
└── go.mod
```

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `MEMORY_PORT` | HTTP port | 8080 |
| `MEMORY_DATA_DIR` | Data directory | ./data |
| `MEMORY_MODEL_PATH` | ONNX model path | (none) |
| `MEMORY_PROVIDER` | Embedding provider | stub |

## Design Notes

This is a Go implementation of the Memory Service designed for GridAgents. See `notes/20260112175440/README.md` for the full design discussion including:

- Hierarchical data structure rationale
- Hybrid search strategy (semantic + exact match)
- Latency budget analysis
- Protocol design decisions

The design targets <10ms p99 latency for both queries and ingestion.

## License

MIT
