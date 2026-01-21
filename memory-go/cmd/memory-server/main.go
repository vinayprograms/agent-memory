// Package main provides the entry point for the memory service.
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"

	"github.com/anthropics/memory-go/internal/api"
	"github.com/anthropics/memory-go/internal/core"
	"github.com/anthropics/memory-go/internal/embedding"
	"github.com/anthropics/memory-go/internal/events"
	"github.com/anthropics/memory-go/internal/search"
	"github.com/anthropics/memory-go/internal/session"
	"github.com/anthropics/memory-go/internal/storage"
	"github.com/anthropics/memory-go/pkg/types"
)

func main() {
	// Parse command line flags
	config := parseFlags()

	// Print startup banner
	printBanner(config)

	// Initialize components
	store, hierarchy, searchEngine, sessions, embedder, emitter, err := initComponents(config)
	if err != nil {
		log.Fatalf("Failed to initialize: %v", err)
	}

	// Create and start server
	server := api.NewServer(
		config.Server,
		store,
		hierarchy,
		searchEngine,
		sessions,
		embedder,
		emitter,
	)

	// Handle shutdown gracefully
	shutdownDone := make(chan struct{})
	go handleShutdown(server, store, emitter, config.Server.ShutdownTimeout, shutdownDone)

	// Start server
	log.Printf("Starting memory service on port %d", config.Server.Port)
	if err := server.Start(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Server error: %v", err)
	}

	// Wait for shutdown to complete
	<-shutdownDone
	log.Println("Memory service stopped")
}

func parseFlags() *types.Config {
	config := types.DefaultConfig()

	// Server flags
	flag.IntVar(&config.Server.Port, "port", config.Server.Port, "HTTP port")
	flag.IntVar(&config.Server.Port, "p", config.Server.Port, "HTTP port (shorthand)")

	// Storage flags
	flag.StringVar(&config.Storage.DataDir, "data-dir", config.Storage.DataDir, "Data directory")
	flag.StringVar(&config.Storage.DataDir, "d", config.Storage.DataDir, "Data directory (shorthand)")
	flag.BoolVar(&config.Storage.SyncWrites, "sync", config.Storage.SyncWrites, "Sync writes to disk")

	// Embedding flags
	flag.StringVar(&config.Embedding.ModelPath, "model", config.Embedding.ModelPath, "ONNX model path")
	flag.StringVar(&config.Embedding.ModelPath, "m", config.Embedding.ModelPath, "ONNX model path (shorthand)")
	flag.StringVar(&config.Embedding.Provider, "provider", config.Embedding.Provider, "Embedding provider (cpu, cuda, coreml, stub)")
	flag.IntVar(&config.Embedding.BatchSize, "batch-size", config.Embedding.BatchSize, "Embedding batch size")

	// Logging flags
	flag.StringVar(&config.Log.Level, "log-level", config.Log.Level, "Log level (trace, debug, info, warn, error)")
	flag.StringVar(&config.Log.Level, "l", config.Log.Level, "Log level (shorthand)")
	flag.StringVar(&config.Log.Format, "log-format", config.Log.Format, "Log format (text, json)")

	// Help
	help := flag.Bool("help", false, "Show help")
	flag.BoolVar(help, "h", false, "Show help (shorthand)")

	flag.Parse()

	if *help {
		printUsage()
		os.Exit(0)
	}

	return config
}

func printUsage() {
	fmt.Print(`Memory Service - Hierarchical memory store for AI agents

Usage:
  memory-server [options]

Options:
  -p, --port PORT          HTTP port (default: 8080)
  -d, --data-dir DIR       Data directory (default: ./data)
  -m, --model PATH         ONNX model path
  --provider PROVIDER      Embedding provider: cpu, cuda, coreml, stub (default: stub)
  --batch-size SIZE        Embedding batch size (default: 32)
  --sync                   Sync writes to disk
  -l, --log-level LEVEL    Log level: trace, debug, info, warn, error (default: info)
  --log-format FORMAT      Log format: text, json (default: text)
  -h, --help               Show this help

Examples:
  # Start with default settings
  memory-server

  # Start on custom port with CUDA
  memory-server -p 9090 --provider cuda

  # Start with custom data directory
  memory-server -d /var/lib/memory

Environment Variables:
  MEMORY_PORT              HTTP port
  MEMORY_DATA_DIR          Data directory
  MEMORY_MODEL_PATH        ONNX model path
  MEMORY_PROVIDER          Embedding provider
`)
}

func printBanner(config *types.Config) {
	fmt.Println(`
╔══════════════════════════════════════════════════════════════╗
║                     Memory Service                           ║
║         Hierarchical Memory Store for AI Agents              ║
╚══════════════════════════════════════════════════════════════╝`)
	fmt.Printf("  Port:      %d\n", config.Server.Port)
	fmt.Printf("  Data Dir:  %s\n", config.Storage.DataDir)
	fmt.Printf("  Provider:  %s\n", config.Embedding.Provider)
	fmt.Println()
}

func initComponents(config *types.Config) (
	*storage.Store,
	*core.HierarchyManager,
	*search.Engine,
	*session.Manager,
	embedding.Engine,
	*events.Emitter,
	error,
) {
	// Ensure data directory exists
	if err := os.MkdirAll(config.Storage.DataDir, 0755); err != nil {
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to create data directory: %w", err)
	}

	// Initialize storage
	log.Println("Initializing storage...")
	store, err := storage.Open(config.Storage)
	if err != nil {
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to open storage: %w", err)
	}

	// Initialize hierarchy manager
	log.Println("Initializing hierarchy manager...")
	hierarchy, err := core.NewHierarchyManager(store)
	if err != nil {
		store.Close()
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to create hierarchy manager: %w", err)
	}

	// Initialize embedding engine
	log.Println("Initializing embedding engine...")
	var embedder embedding.Engine

	// Use stub if no model path or provider is stub
	if config.Embedding.ModelPath == "" || config.Embedding.Provider == "stub" || config.Embedding.Provider == "" {
		log.Println("Using stub embedding engine (for testing)")
		embedder = embedding.NewStubEngine()
	} else {
		embedder, err = embedding.NewEngine(config.Embedding)
		if err != nil {
			// Fall back to stub
			log.Printf("Warning: Failed to initialize ONNX engine: %v, using stub", err)
			embedder = embedding.NewStubEngine()
		}
	}

	// Initialize session manager
	log.Println("Initializing session manager...")
	sessions, err := session.NewManager(store)
	if err != nil {
		store.Close()
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to create session manager: %w", err)
	}

	// Initialize search engine
	log.Println("Initializing search engine...")
	searchEngine, err := search.NewEngine(store, embedder, config.Search)
	if err != nil {
		store.Close()
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to create search engine: %w", err)
	}

	// Initialize event emitter
	log.Println("Initializing event emitter...")
	eventsDir := filepath.Join(config.Storage.DataDir, "events")
	emitter, err := events.NewEmitter(eventsDir)
	if err != nil {
		store.Close()
		return nil, nil, nil, nil, nil, nil, fmt.Errorf("failed to create event emitter: %w", err)
	}

	log.Println("All components initialized successfully")

	return store, hierarchy, searchEngine, sessions, embedder, emitter, nil
}

func handleShutdown(server *api.Server, store *storage.Store, emitter *events.Emitter, timeout interface{}, done chan struct{}) {
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	<-sigChan
	log.Println("Shutdown signal received, stopping server...")

	// Create context with timeout
	ctx, cancel := context.WithTimeout(context.Background(), types.DefaultConfig().Server.ShutdownTimeout)
	defer cancel()

	// Shutdown server
	if err := server.Shutdown(ctx); err != nil {
		log.Printf("Server shutdown error: %v", err)
	}

	// Flush and close event emitter
	if emitter != nil {
		emitter.Flush()
		emitter.Close()
	}

	// Flush and close storage
	if store != nil {
		log.Println("Flushing storage...")
		if err := store.Flush(); err != nil {
			log.Printf("Storage flush error: %v", err)
		}
		if err := store.Close(); err != nil {
			log.Printf("Storage close error: %v", err)
		}
	}

	log.Println("Shutdown complete")
	close(done)
}
