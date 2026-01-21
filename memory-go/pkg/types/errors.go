package types

import (
	"errors"
	"fmt"
)

// Sentinel errors for the memory service.
var (
	// General errors
	ErrNotFound      = errors.New("not found")
	ErrAlreadyExists = errors.New("already exists")
	ErrInvalidArg    = errors.New("invalid argument")
	ErrFull          = errors.New("capacity full")
	ErrEmpty         = errors.New("empty")
	ErrClosed        = errors.New("closed")

	// Storage errors
	ErrStorageCorrupt = errors.New("storage corrupted")
	ErrStorageIO      = errors.New("storage I/O error")

	// Hierarchy errors
	ErrInvalidLevel  = errors.New("invalid hierarchy level")
	ErrOrphanNode    = errors.New("orphan node")
	ErrCycleDetected = errors.New("cycle detected in hierarchy")

	// Search errors
	ErrSearchFailed   = errors.New("search failed")
	ErrEmbeddingFailed = errors.New("embedding generation failed")

	// API errors
	ErrInvalidJSON   = errors.New("invalid JSON")
	ErrInvalidMethod = errors.New("invalid method")
	ErrInvalidParams = errors.New("invalid params")
)

// Error wraps an error with additional context.
type Error struct {
	Op      string // Operation that failed
	Kind    error  // Category of error
	Err     error  // Underlying error
	Message string // Human-readable message
}

func (e *Error) Error() string {
	if e.Message != "" {
		return fmt.Sprintf("%s: %s: %v", e.Op, e.Message, e.Err)
	}
	if e.Err != nil {
		return fmt.Sprintf("%s: %v", e.Op, e.Err)
	}
	return fmt.Sprintf("%s: %v", e.Op, e.Kind)
}

func (e *Error) Unwrap() error {
	return e.Err
}

func (e *Error) Is(target error) bool {
	return errors.Is(e.Kind, target)
}

// Errorf creates a new Error with formatted message.
func Errorf(op string, kind error, format string, args ...any) error {
	return &Error{
		Op:      op,
		Kind:    kind,
		Message: fmt.Sprintf(format, args...),
	}
}

// WrapError wraps an error with operation context.
func WrapError(op string, kind error, err error) error {
	return &Error{
		Op:   op,
		Kind: kind,
		Err:  err,
	}
}

// JSON-RPC error codes (per JSON-RPC 2.0 spec)
const (
	RPCParseError     = -32700 // Invalid JSON
	RPCInvalidRequest = -32600 // Invalid request object
	RPCMethodNotFound = -32601 // Method not found
	RPCInvalidParams  = -32602 // Invalid method parameters
	RPCInternalError  = -32603 // Internal error
)

// RPCError represents a JSON-RPC error.
type RPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    any    `json:"data,omitempty"`
}

func (e *RPCError) Error() string {
	return fmt.Sprintf("RPC error %d: %s", e.Code, e.Message)
}

// NewRPCError creates a new RPCError.
func NewRPCError(code int, message string, data any) *RPCError {
	return &RPCError{
		Code:    code,
		Message: message,
		Data:    data,
	}
}
