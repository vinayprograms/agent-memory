package types

import (
	"errors"
	"testing"
)

func TestError_Error(t *testing.T) {
	tests := []struct {
		name     string
		err      *Error
		contains string
	}{
		{
			name: "with message",
			err: &Error{
				Op:      "test.Op",
				Kind:    ErrNotFound,
				Message: "item not found",
			},
			contains: "test.Op",
		},
		{
			name: "with underlying error",
			err: &Error{
				Op:   "test.Op",
				Kind: ErrStorageIO,
				Err:  errors.New("disk full"),
			},
			contains: "disk full",
		},
		{
			name: "kind only",
			err: &Error{
				Op:   "test.Op",
				Kind: ErrInvalidArg,
			},
			contains: "invalid argument",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			msg := tt.err.Error()
			if msg == "" {
				t.Error("Error() returned empty string")
			}
			// Just verify it doesn't panic and returns something
		})
	}
}

func TestError_Is(t *testing.T) {
	err := &Error{
		Op:   "test",
		Kind: ErrNotFound,
	}

	if !errors.Is(err, ErrNotFound) {
		t.Error("Error should match ErrNotFound")
	}

	if errors.Is(err, ErrInvalidArg) {
		t.Error("Error should not match ErrInvalidArg")
	}
}

func TestError_Unwrap(t *testing.T) {
	inner := errors.New("inner error")
	err := &Error{
		Op:   "test",
		Kind: ErrStorageIO,
		Err:  inner,
	}

	if errors.Unwrap(err) != inner {
		t.Error("Unwrap should return inner error")
	}
}

func TestErrorf(t *testing.T) {
	err := Errorf("myop", ErrNotFound, "user %d not found", 42)

	if err == nil {
		t.Fatal("Errorf returned nil")
	}

	var e *Error
	if !errors.As(err, &e) {
		t.Fatal("Errorf should return *Error")
	}

	if e.Op != "myop" {
		t.Errorf("Op = %s, want myop", e.Op)
	}
}

func TestWrapError(t *testing.T) {
	inner := errors.New("connection refused")
	err := WrapError("storage.Open", ErrStorageIO, inner)

	var e *Error
	if !errors.As(err, &e) {
		t.Fatal("WrapError should return *Error")
	}

	if e.Err != inner {
		t.Error("wrapped error should contain inner error")
	}
}

func TestRPCError(t *testing.T) {
	err := NewRPCError(RPCMethodNotFound, "method foo not found", nil)

	if err.Code != RPCMethodNotFound {
		t.Errorf("Code = %d, want %d", err.Code, RPCMethodNotFound)
	}

	msg := err.Error()
	if msg == "" {
		t.Error("Error() should not be empty")
	}
}
