.PHONY: all build build-mcp build-inspect run test clean fmt lint vet docker

# Binary names
BINARY=memory-server
MCP_BINARY=memory-mcp
INSPECT_BINARY=memory-inspect

# Build directory
BUILD_DIR=./build

# Go parameters
GOCMD=go
GOBUILD=$(GOCMD) build
GORUN=$(GOCMD) run
GOTEST=$(GOCMD) test
GOCLEAN=$(GOCMD) clean
GOGET=$(GOCMD) get
GOMOD=$(GOCMD) mod
GOFMT=gofmt
GOVET=$(GOCMD) vet

# Build flags
LDFLAGS=-ldflags "-s -w"

# Main packages
MAIN_PKG=./cmd/memory-server
MCP_PKG=./cmd/memory-mcp
INSPECT_PKG=./cmd/memory-inspect

all: build build-mcp build-inspect

# Build the HTTP server binary
build:
	@echo "Building $(BINARY)..."
	@mkdir -p $(BUILD_DIR)
	$(GOBUILD) $(LDFLAGS) -o $(BUILD_DIR)/$(BINARY) $(MAIN_PKG)
	@echo "Build complete: $(BUILD_DIR)/$(BINARY)"

# Build the MCP server binary
build-mcp:
	@echo "Building $(MCP_BINARY)..."
	@mkdir -p $(BUILD_DIR)
	$(GOBUILD) $(LDFLAGS) -o $(BUILD_DIR)/$(MCP_BINARY) $(MCP_PKG)
	@echo "Build complete: $(BUILD_DIR)/$(MCP_BINARY)"

# Build the inspect CLI tool
build-inspect:
	@echo "Building $(INSPECT_BINARY)..."
	@mkdir -p $(BUILD_DIR)
	$(GOBUILD) $(LDFLAGS) -o $(BUILD_DIR)/$(INSPECT_BINARY) $(INSPECT_PKG)
	@echo "Build complete: $(BUILD_DIR)/$(INSPECT_BINARY)"

# Build with debug info
build-debug:
	@echo "Building $(BINARY) with debug info..."
	@mkdir -p $(BUILD_DIR)
	$(GOBUILD) -o $(BUILD_DIR)/$(BINARY) $(MAIN_PKG)

# Run the server
run:
	$(GORUN) $(MAIN_PKG)

# Run with custom port
run-dev:
	$(GORUN) $(MAIN_PKG) -p 8081 --provider stub -l debug

# Run tests
test:
	$(GOTEST) -v ./...

# Run tests with coverage
test-coverage:
	$(GOTEST) -v -coverprofile=coverage.out ./...
	$(GOCMD) tool cover -html=coverage.out -o coverage.html

# Run benchmarks
bench:
	$(GOTEST) -bench=. -benchmem ./...

# Clean build artifacts
clean:
	@echo "Cleaning..."
	$(GOCLEAN)
	rm -rf $(BUILD_DIR)
	rm -f coverage.out coverage.html

# Format code
fmt:
	$(GOFMT) -s -w .

# Lint code (requires golangci-lint)
lint:
	golangci-lint run ./...

# Vet code
vet:
	$(GOVET) ./...

# Download dependencies
deps:
	$(GOMOD) download

# Tidy dependencies
tidy:
	$(GOMOD) tidy

# Update dependencies
update:
	$(GOGET) -u ./...
	$(GOMOD) tidy

# Build Docker image
docker:
	docker build -t memory-service:latest .

# Run Docker container
docker-run:
	docker run -p 8080:8080 -v $(PWD)/data:/app/data memory-service:latest

# Generate mocks (if needed)
mocks:
	@echo "Generating mocks..."
	# go generate ./...

# Show help
help:
	@echo "Memory Service Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make              Build all binaries (server + MCP + inspect)"
	@echo "  make build        Build the HTTP server binary"
	@echo "  make build-mcp    Build the MCP server binary"
	@echo "  make build-inspect Build the database inspector CLI"
	@echo "  make build-debug  Build with debug info"
	@echo "  make run          Run the HTTP server"
	@echo "  make run-dev      Run in development mode"
	@echo "  make test         Run tests"
	@echo "  make test-coverage Run tests with coverage"
	@echo "  make bench        Run benchmarks"
	@echo "  make clean        Clean build artifacts"
	@echo "  make fmt          Format code"
	@echo "  make lint         Lint code"
	@echo "  make vet          Vet code"
	@echo "  make deps         Download dependencies"
	@echo "  make tidy         Tidy dependencies"
	@echo "  make docker       Build Docker image"
	@echo "  make docker-run   Run Docker container"
	@echo "  make help         Show this help"
