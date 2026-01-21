// Package session provides session management for the memory service.
package session

import (
	"sync"
	"time"

	"github.com/anthropics/memory-go/internal/storage"
	"github.com/anthropics/memory-go/pkg/types"
)

// Manager handles session lifecycle and metadata.
type Manager struct {
	store    *storage.Store
	sessions map[string]*types.Session // In-memory cache
	mu       sync.RWMutex
}

// NewManager creates a new session manager.
func NewManager(store *storage.Store) (*Manager, error) {
	m := &Manager{
		store:    store,
		sessions: make(map[string]*types.Session),
	}

	// Load existing sessions into cache
	if err := m.loadSessions(); err != nil {
		return nil, err
	}

	return m, nil
}

// loadSessions loads all sessions from storage into the cache.
func (m *Manager) loadSessions() error {
	sessions, err := m.store.ListSessions()
	if err != nil {
		return err
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	for _, s := range sessions {
		m.sessions[s.ID] = s
	}

	return nil
}

// GetOrCreate retrieves an existing session or creates a new one.
func (m *Manager) GetOrCreate(sessionID, agentID string) (*types.Session, bool, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// Check cache first
	if session, exists := m.sessions[sessionID]; exists {
		// Update last active time
		session.LastActiveAt = time.Now()
		session.SequenceNum = m.store.NextSequence()
		m.store.SaveSession(session)
		return session, false, nil
	}

	// Create new session
	now := time.Now()
	rootNodeID := m.store.NextNodeID()

	// Create actual root node for the session
	rootNode := &types.Node{
		ID:          rootNodeID,
		Level:       types.LevelSession,
		ParentID:    0, // Root has no parent
		AgentID:     agentID,
		SessionID:   sessionID,
		Content:     "Session: " + sessionID,
		CreatedAt:   now,
		SequenceNum: m.store.NextSequence(),
	}
	if err := m.store.SaveNode(rootNode); err != nil {
		return nil, false, err
	}

	session := &types.Session{
		ID:           sessionID,
		AgentID:      agentID,
		RootNodeID:   rootNodeID,
		CreatedAt:    now,
		LastActiveAt: now,
		SequenceNum:  m.store.NextSequence(),
		Keywords:     make([]string, 0),
		Identifiers:  make([]string, 0),
		FilesTouched: make([]string, 0),
	}

	// Persist
	if err := m.store.SaveSession(session); err != nil {
		return nil, false, err
	}

	// Cache
	m.sessions[sessionID] = session

	return session, true, nil
}

// Get retrieves a session by ID.
func (m *Manager) Get(sessionID string) (*types.Session, error) {
	m.mu.RLock()
	session, exists := m.sessions[sessionID]
	m.mu.RUnlock()

	if exists {
		return session, nil
	}

	// Try storage
	session, err := m.store.GetSession(sessionID)
	if err != nil {
		return nil, err
	}

	// Cache it
	m.mu.Lock()
	m.sessions[sessionID] = session
	m.mu.Unlock()

	return session, nil
}

// Update updates a session's metadata.
func (m *Manager) Update(session *types.Session) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session.LastActiveAt = time.Now()
	session.SequenceNum = m.store.NextSequence()

	if err := m.store.SaveSession(session); err != nil {
		return err
	}

	m.sessions[session.ID] = session
	return nil
}

// Delete removes a session.
func (m *Manager) Delete(sessionID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if err := m.store.DeleteSession(sessionID); err != nil {
		return err
	}

	delete(m.sessions, sessionID)
	return nil
}

// List returns all sessions.
func (m *Manager) List() []*types.Session {
	m.mu.RLock()
	defer m.mu.RUnlock()

	sessions := make([]*types.Session, 0, len(m.sessions))
	for _, s := range m.sessions {
		sessions = append(sessions, s)
	}

	return sessions
}

// ListByAgent returns all sessions for a specific agent.
func (m *Manager) ListByAgent(agentID string) []*types.Session {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var sessions []*types.Session
	for _, s := range m.sessions {
		if s.AgentID == agentID {
			sessions = append(sessions, s)
		}
	}

	return sessions
}

// AddKeywords adds keywords to a session (deduplicating).
func (m *Manager) AddKeywords(sessionID string, keywords []string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, exists := m.sessions[sessionID]
	if !exists {
		return types.ErrNotFound
	}

	// Deduplicate and append
	existing := make(map[string]struct{})
	for _, k := range session.Keywords {
		existing[k] = struct{}{}
	}

	for _, k := range keywords {
		if _, found := existing[k]; !found {
			if len(session.Keywords) < types.MaxKeywords {
				session.Keywords = append(session.Keywords, k)
				existing[k] = struct{}{}
			}
		}
	}

	return m.store.SaveSession(session)
}

// AddIdentifiers adds identifiers to a session.
func (m *Manager) AddIdentifiers(sessionID string, identifiers []string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, exists := m.sessions[sessionID]
	if !exists {
		return types.ErrNotFound
	}

	existing := make(map[string]struct{})
	for _, id := range session.Identifiers {
		existing[id] = struct{}{}
	}

	for _, id := range identifiers {
		if _, found := existing[id]; !found {
			if len(session.Identifiers) < types.MaxIdentifiers {
				session.Identifiers = append(session.Identifiers, id)
				existing[id] = struct{}{}
			}
		}
	}

	return m.store.SaveSession(session)
}

// AddFilesTouched adds file paths to a session.
func (m *Manager) AddFilesTouched(sessionID string, files []string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, exists := m.sessions[sessionID]
	if !exists {
		return types.ErrNotFound
	}

	existing := make(map[string]struct{})
	for _, f := range session.FilesTouched {
		existing[f] = struct{}{}
	}

	for _, f := range files {
		if _, found := existing[f]; !found {
			if len(session.FilesTouched) < types.MaxFilesTouched {
				session.FilesTouched = append(session.FilesTouched, f)
				existing[f] = struct{}{}
			}
		}
	}

	return m.store.SaveSession(session)
}

// SetTitle sets the session title.
func (m *Manager) SetTitle(sessionID, title string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, exists := m.sessions[sessionID]
	if !exists {
		return types.ErrNotFound
	}

	session.Title = title
	return m.store.SaveSession(session)
}

// Touch updates the last active timestamp.
func (m *Manager) Touch(sessionID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	session, exists := m.sessions[sessionID]
	if !exists {
		return types.ErrNotFound
	}

	session.LastActiveAt = time.Now()
	session.SequenceNum = m.store.NextSequence()
	return m.store.SaveSession(session)
}

// Count returns the total number of sessions.
func (m *Manager) Count() int {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return len(m.sessions)
}

// Stats returns session statistics.
func (m *Manager) Stats() map[string]interface{} {
	m.mu.RLock()
	defer m.mu.RUnlock()

	agents := make(map[string]int)
	for _, s := range m.sessions {
		agents[s.AgentID]++
	}

	return map[string]interface{}{
		"total_sessions": len(m.sessions),
		"agents":         len(agents),
	}
}
