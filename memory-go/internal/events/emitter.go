// Package events provides event emission for the memory service.
package events

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/anthropics/memory-go/pkg/types"
	"github.com/google/uuid"
)

// EventType represents the type of event.
type EventType string

const (
	MemoryStored    EventType = "memory_stored"
	MemoryDeleted   EventType = "memory_deleted"
	SessionCreated  EventType = "session_created"
	SessionUpdated  EventType = "session_updated"
	QueryPerformed  EventType = "query_performed"
)

// Event represents a memory service event.
type Event struct {
	ID        string                 `json:"id"`
	Type      EventType              `json:"type"`
	NodeID    types.NodeID           `json:"node_id,omitempty"`
	SessionID string                 `json:"session_id,omitempty"`
	AgentID   string                 `json:"agent_id,omitempty"`
	Timestamp time.Time              `json:"timestamp"`
	TraceID   string                 `json:"trace_id,omitempty"`
	Data      map[string]interface{} `json:"data,omitempty"`
}

// Subscriber is a function that handles events.
type Subscriber func(Event)

// Emitter handles event emission and subscription.
type Emitter struct {
	subscribers []Subscriber
	file        *os.File
	filePath    string
	mu          sync.RWMutex
	enabled     bool
}

// NewEmitter creates a new event emitter.
func NewEmitter(eventsDir string) (*Emitter, error) {
	e := &Emitter{
		subscribers: make([]Subscriber, 0),
		enabled:     true,
	}

	if eventsDir != "" {
		if err := os.MkdirAll(eventsDir, 0755); err != nil {
			return nil, types.WrapError("events.NewEmitter", types.ErrStorageIO, err)
		}

		// Create events file with timestamp
		filename := fmt.Sprintf("events_%s.jsonl", time.Now().Format("20060102"))
		e.filePath = filepath.Join(eventsDir, filename)

		file, err := os.OpenFile(e.filePath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
		if err != nil {
			return nil, types.WrapError("events.NewEmitter", types.ErrStorageIO, err)
		}
		e.file = file
	}

	return e, nil
}

// Subscribe adds a subscriber to receive events.
func (e *Emitter) Subscribe(sub Subscriber) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.subscribers = append(e.subscribers, sub)
}

// Emit emits an event to all subscribers and the file.
func (e *Emitter) Emit(event Event) {
	if !e.enabled {
		return
	}

	// Generate ID if not set
	if event.ID == "" {
		event.ID = uuid.New().String()
	}

	// Set timestamp if not set
	if event.Timestamp.IsZero() {
		event.Timestamp = time.Now()
	}

	e.mu.RLock()
	subscribers := make([]Subscriber, len(e.subscribers))
	copy(subscribers, e.subscribers)
	e.mu.RUnlock()

	// Notify subscribers (non-blocking)
	for _, sub := range subscribers {
		go sub(event)
	}

	// Write to file
	e.writeToFile(event)
}

// EmitWithTrace emits an event with a trace ID for correlation.
func (e *Emitter) EmitWithTrace(event Event, traceID string) {
	event.TraceID = traceID
	e.Emit(event)
}

// writeToFile writes an event to the JSON Lines file.
func (e *Emitter) writeToFile(event Event) {
	if e.file == nil {
		return
	}

	data, err := json.Marshal(event)
	if err != nil {
		return
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	e.file.Write(data)
	e.file.Write([]byte("\n"))
}

// Flush ensures all events are written to disk.
func (e *Emitter) Flush() error {
	if e.file == nil {
		return nil
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	return e.file.Sync()
}

// Close closes the emitter.
func (e *Emitter) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.enabled = false

	if e.file != nil {
		return e.file.Close()
	}

	return nil
}

// Enable enables event emission.
func (e *Emitter) Enable() {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.enabled = true
}

// Disable disables event emission.
func (e *Emitter) Disable() {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.enabled = false
}

// IsEnabled returns whether the emitter is enabled.
func (e *Emitter) IsEnabled() bool {
	e.mu.RLock()
	defer e.mu.RUnlock()
	return e.enabled
}

// RotateFile rotates the events file.
func (e *Emitter) RotateFile() error {
	if e.file == nil || e.filePath == "" {
		return nil
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	// Close old file
	if err := e.file.Close(); err != nil {
		return err
	}

	// Create new file with current timestamp
	dir := filepath.Dir(e.filePath)
	filename := fmt.Sprintf("events_%s.jsonl", time.Now().Format("20060102_150405"))
	e.filePath = filepath.Join(dir, filename)

	file, err := os.OpenFile(e.filePath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}

	e.file = file
	return nil
}

// NewTraceID generates a new trace ID.
func NewTraceID() string {
	return uuid.New().String()
}

// MemoryStoredEvent creates a memory stored event.
func MemoryStoredEvent(nodeID types.NodeID, sessionID, agentID string) Event {
	return Event{
		Type:      MemoryStored,
		NodeID:    nodeID,
		SessionID: sessionID,
		AgentID:   agentID,
		Timestamp: time.Now(),
	}
}

// MemoryDeletedEvent creates a memory deleted event.
func MemoryDeletedEvent(nodeID types.NodeID, sessionID, agentID string) Event {
	return Event{
		Type:      MemoryDeleted,
		NodeID:    nodeID,
		SessionID: sessionID,
		AgentID:   agentID,
		Timestamp: time.Now(),
	}
}

// SessionCreatedEvent creates a session created event.
func SessionCreatedEvent(sessionID, agentID string) Event {
	return Event{
		Type:      SessionCreated,
		SessionID: sessionID,
		AgentID:   agentID,
		Timestamp: time.Now(),
	}
}

// QueryPerformedEvent creates a query performed event.
func QueryPerformedEvent(sessionID, agentID, query string, resultCount int) Event {
	return Event{
		Type:      QueryPerformed,
		SessionID: sessionID,
		AgentID:   agentID,
		Timestamp: time.Now(),
		Data: map[string]interface{}{
			"query":        query,
			"result_count": resultCount,
		},
	}
}
