// Package storage provides persistent storage using Pebble.
package storage

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"sync"
	"sync/atomic"

	"github.com/anthropics/memory-go/pkg/types"
	"github.com/cockroachdb/pebble"
)

// Key prefixes for different data types in Pebble.
const (
	prefixNode      byte = 0x01 // node:<id> -> Node JSON
	prefixEmbedding byte = 0x02 // emb:<level>:<id> -> []float32 binary
	prefixSession   byte = 0x03 // sess:<session_id> -> Session JSON
	prefixMeta      byte = 0x04 // meta:<key> -> value
	prefixIndex     byte = 0x05 // idx:<type>:<key> -> value
)

// Meta keys
const (
	metaNodeCounter = "node_counter"
	metaSeqCounter  = "seq_counter"
)

// Store provides persistent storage for the memory service.
type Store struct {
	db          *pebble.DB
	nodeCounter atomic.Uint64
	seqCounter  atomic.Uint64
	config      types.StorageConfig
	mu          sync.RWMutex
	closed      atomic.Bool
}

// Open opens or creates a store at the given path.
func Open(config types.StorageConfig) (*Store, error) {
	opts := &pebble.Options{
		Cache:        pebble.NewCache(config.CacheSize),
		MaxOpenFiles: 1000,
	}

	if config.SyncWrites {
		opts.WALDir = config.DataDir + "/wal"
	}

	db, err := pebble.Open(config.DataDir, opts)
	if err != nil {
		return nil, types.WrapError("storage.Open", types.ErrStorageIO, err)
	}

	s := &Store{
		db:     db,
		config: config,
	}

	// Load counters from storage
	if err := s.loadCounters(); err != nil {
		db.Close()
		return nil, err
	}

	return s, nil
}

// Close closes the store.
func (s *Store) Close() error {
	if s.closed.Swap(true) {
		return nil // Already closed
	}

	// Persist counters before closing
	if err := s.saveCounters(); err != nil {
		return err
	}

	return s.db.Close()
}

// loadCounters loads the node and sequence counters from storage.
func (s *Store) loadCounters() error {
	// Load node counter
	nodeVal, closer, err := s.db.Get(s.metaKey(metaNodeCounter))
	if err == pebble.ErrNotFound {
		s.nodeCounter.Store(0)
	} else if err != nil {
		return types.WrapError("storage.loadCounters", types.ErrStorageIO, err)
	} else {
		s.nodeCounter.Store(binary.BigEndian.Uint64(nodeVal))
		closer.Close()
	}

	// Load sequence counter
	seqVal, closer, err := s.db.Get(s.metaKey(metaSeqCounter))
	if err == pebble.ErrNotFound {
		s.seqCounter.Store(0)
	} else if err != nil {
		return types.WrapError("storage.loadCounters", types.ErrStorageIO, err)
	} else {
		s.seqCounter.Store(binary.BigEndian.Uint64(seqVal))
		closer.Close()
	}

	return nil
}

// saveCounters persists the node and sequence counters.
func (s *Store) saveCounters() error {
	batch := s.db.NewBatch()
	defer batch.Close()

	nodeVal := make([]byte, 8)
	binary.BigEndian.PutUint64(nodeVal, s.nodeCounter.Load())
	if err := batch.Set(s.metaKey(metaNodeCounter), nodeVal, pebble.Sync); err != nil {
		return types.WrapError("storage.saveCounters", types.ErrStorageIO, err)
	}

	seqVal := make([]byte, 8)
	binary.BigEndian.PutUint64(seqVal, s.seqCounter.Load())
	if err := batch.Set(s.metaKey(metaSeqCounter), seqVal, pebble.Sync); err != nil {
		return types.WrapError("storage.saveCounters", types.ErrStorageIO, err)
	}

	return batch.Commit(pebble.Sync)
}

// NextNodeID returns the next available node ID.
func (s *Store) NextNodeID() types.NodeID {
	return types.NodeID(s.nodeCounter.Add(1))
}

// NextSequence returns the next sequence number.
func (s *Store) NextSequence() uint64 {
	return s.seqCounter.Add(1)
}

// Key generation helpers

func (s *Store) nodeKey(id types.NodeID) []byte {
	key := make([]byte, 9)
	key[0] = prefixNode
	binary.BigEndian.PutUint64(key[1:], uint64(id))
	return key
}

func (s *Store) embeddingKey(level types.HierarchyLevel, id types.NodeID) []byte {
	key := make([]byte, 10)
	key[0] = prefixEmbedding
	key[1] = byte(level)
	binary.BigEndian.PutUint64(key[2:], uint64(id))
	return key
}

func (s *Store) sessionKey(sessionID string) []byte {
	key := make([]byte, 1+len(sessionID))
	key[0] = prefixSession
	copy(key[1:], sessionID)
	return key
}

func (s *Store) metaKey(name string) []byte {
	key := make([]byte, 1+len(name))
	key[0] = prefixMeta
	copy(key[1:], name)
	return key
}

// Node operations

// SaveNode persists a node.
func (s *Store) SaveNode(node *types.Node) error {
	data, err := json.Marshal(node)
	if err != nil {
		return types.WrapError("storage.SaveNode", types.ErrInvalidArg, err)
	}

	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Set(s.nodeKey(node.ID), data, writeOpts); err != nil {
		return types.WrapError("storage.SaveNode", types.ErrStorageIO, err)
	}

	return nil
}

// GetNode retrieves a node by ID.
func (s *Store) GetNode(id types.NodeID) (*types.Node, error) {
	data, closer, err := s.db.Get(s.nodeKey(id))
	if err == pebble.ErrNotFound {
		return nil, types.ErrNotFound
	}
	if err != nil {
		return nil, types.WrapError("storage.GetNode", types.ErrStorageIO, err)
	}
	defer closer.Close()

	var node types.Node
	if err := json.Unmarshal(data, &node); err != nil {
		return nil, types.WrapError("storage.GetNode", types.ErrStorageCorrupt, err)
	}

	return &node, nil
}

// DeleteNode removes a node.
func (s *Store) DeleteNode(id types.NodeID) error {
	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Delete(s.nodeKey(id), writeOpts); err != nil {
		return types.WrapError("storage.DeleteNode", types.ErrStorageIO, err)
	}

	return nil
}

// Embedding operations

// SaveEmbedding persists an embedding vector.
func (s *Store) SaveEmbedding(level types.HierarchyLevel, id types.NodeID, embedding types.Embedding) error {
	// Convert float32 slice to bytes
	buf := make([]byte, len(embedding)*4)
	for i, v := range embedding {
		binary.LittleEndian.PutUint32(buf[i*4:], math.Float32bits(v))
	}

	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Set(s.embeddingKey(level, id), buf, writeOpts); err != nil {
		return types.WrapError("storage.SaveEmbedding", types.ErrStorageIO, err)
	}

	return nil
}

// GetEmbedding retrieves an embedding vector.
func (s *Store) GetEmbedding(level types.HierarchyLevel, id types.NodeID) (types.Embedding, error) {
	data, closer, err := s.db.Get(s.embeddingKey(level, id))
	if err == pebble.ErrNotFound {
		return nil, types.ErrNotFound
	}
	if err != nil {
		return nil, types.WrapError("storage.GetEmbedding", types.ErrStorageIO, err)
	}
	defer closer.Close()

	// Convert bytes to float32 slice
	embedding := make(types.Embedding, len(data)/4)
	for i := range embedding {
		bits := binary.LittleEndian.Uint32(data[i*4:])
		embedding[i] = math.Float32frombits(bits)
	}

	return embedding, nil
}

// DeleteEmbedding removes an embedding.
func (s *Store) DeleteEmbedding(level types.HierarchyLevel, id types.NodeID) error {
	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Delete(s.embeddingKey(level, id), writeOpts); err != nil {
		return types.WrapError("storage.DeleteEmbedding", types.ErrStorageIO, err)
	}

	return nil
}

// Session operations

// SaveSession persists a session.
func (s *Store) SaveSession(session *types.Session) error {
	data, err := json.Marshal(session)
	if err != nil {
		return types.WrapError("storage.SaveSession", types.ErrInvalidArg, err)
	}

	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Set(s.sessionKey(session.ID), data, writeOpts); err != nil {
		return types.WrapError("storage.SaveSession", types.ErrStorageIO, err)
	}

	return nil
}

// GetSession retrieves a session by ID.
func (s *Store) GetSession(sessionID string) (*types.Session, error) {
	data, closer, err := s.db.Get(s.sessionKey(sessionID))
	if err == pebble.ErrNotFound {
		return nil, types.ErrNotFound
	}
	if err != nil {
		return nil, types.WrapError("storage.GetSession", types.ErrStorageIO, err)
	}
	defer closer.Close()

	var session types.Session
	if err := json.Unmarshal(data, &session); err != nil {
		return nil, types.WrapError("storage.GetSession", types.ErrStorageCorrupt, err)
	}

	return &session, nil
}

// DeleteSession removes a session.
func (s *Store) DeleteSession(sessionID string) error {
	writeOpts := pebble.NoSync
	if s.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := s.db.Delete(s.sessionKey(sessionID), writeOpts); err != nil {
		return types.WrapError("storage.DeleteSession", types.ErrStorageIO, err)
	}

	return nil
}

// ListSessions returns all sessions.
func (s *Store) ListSessions() ([]*types.Session, error) {
	var sessions []*types.Session

	iter, err := s.db.NewIter(&pebble.IterOptions{
		LowerBound: []byte{prefixSession},
		UpperBound: []byte{prefixSession + 1},
	})
	if err != nil {
		return nil, types.WrapError("storage.ListSessions", types.ErrStorageIO, err)
	}
	defer iter.Close()

	for iter.First(); iter.Valid(); iter.Next() {
		var session types.Session
		if err := json.Unmarshal(iter.Value(), &session); err != nil {
			continue // Skip corrupted entries
		}
		sessions = append(sessions, &session)
	}

	if err := iter.Error(); err != nil {
		return nil, types.WrapError("storage.ListSessions", types.ErrStorageIO, err)
	}

	return sessions, nil
}

// Batch operations

// Batch represents a batch of operations.
type Batch struct {
	store *Store
	batch *pebble.Batch
}

// NewBatch creates a new batch.
func (s *Store) NewBatch() *Batch {
	return &Batch{
		store: s,
		batch: s.db.NewBatch(),
	}
}

// SaveNode adds a node save to the batch.
func (b *Batch) SaveNode(node *types.Node) error {
	data, err := json.Marshal(node)
	if err != nil {
		return types.WrapError("batch.SaveNode", types.ErrInvalidArg, err)
	}
	return b.batch.Set(b.store.nodeKey(node.ID), data, nil)
}

// SaveEmbedding adds an embedding save to the batch.
func (b *Batch) SaveEmbedding(level types.HierarchyLevel, id types.NodeID, embedding types.Embedding) error {
	buf := make([]byte, len(embedding)*4)
	for i, v := range embedding {
		binary.LittleEndian.PutUint32(buf[i*4:], math.Float32bits(v))
	}
	return b.batch.Set(b.store.embeddingKey(level, id), buf, nil)
}

// SaveSession adds a session save to the batch.
func (b *Batch) SaveSession(session *types.Session) error {
	data, err := json.Marshal(session)
	if err != nil {
		return types.WrapError("batch.SaveSession", types.ErrInvalidArg, err)
	}
	return b.batch.Set(b.store.sessionKey(session.ID), data, nil)
}

// Commit commits the batch.
func (b *Batch) Commit() error {
	writeOpts := pebble.NoSync
	if b.store.config.SyncWrites {
		writeOpts = pebble.Sync
	}

	if err := b.batch.Commit(writeOpts); err != nil {
		return types.WrapError("batch.Commit", types.ErrStorageIO, err)
	}

	return nil
}

// Close discards the batch without committing.
func (b *Batch) Close() error {
	return b.batch.Close()
}

// Iterator helpers

// IterateNodes iterates over all nodes, calling fn for each.
func (s *Store) IterateNodes(fn func(*types.Node) error) error {
	iter, err := s.db.NewIter(&pebble.IterOptions{
		LowerBound: []byte{prefixNode},
		UpperBound: []byte{prefixNode + 1},
	})
	if err != nil {
		return types.WrapError("storage.IterateNodes", types.ErrStorageIO, err)
	}
	defer iter.Close()

	for iter.First(); iter.Valid(); iter.Next() {
		var node types.Node
		if err := json.Unmarshal(iter.Value(), &node); err != nil {
			continue // Skip corrupted entries
		}
		if err := fn(&node); err != nil {
			return err
		}
	}

	return iter.Error()
}

// GetChildNodes returns all child nodes of a parent.
func (s *Store) GetChildNodes(parentID types.NodeID) ([]*types.Node, error) {
	var children []*types.Node

	err := s.IterateNodes(func(node *types.Node) error {
		if node.ParentID == parentID {
			children = append(children, node)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}

	return children, nil
}

// GetNodesBySession returns nodes in a session, with optional limit.
func (s *Store) GetNodesBySession(sessionID string, limit int) ([]*types.Node, error) {
	var nodes []*types.Node

	err := s.IterateNodes(func(node *types.Node) error {
		if node.SessionID == sessionID {
			nodes = append(nodes, node)
			if limit > 0 && len(nodes) >= limit {
				return errStopIteration
			}
		}
		return nil
	})
	if err != nil && err != errStopIteration {
		return nil, err
	}

	return nodes, nil
}

// ListNodes returns all nodes up to the given limit.
func (s *Store) ListNodes(limit int) ([]*types.Node, error) {
	var nodes []*types.Node

	err := s.IterateNodes(func(node *types.Node) error {
		nodes = append(nodes, node)
		if limit > 0 && len(nodes) >= limit {
			return errStopIteration
		}
		return nil
	})
	if err != nil && err != errStopIteration {
		return nil, err
	}

	return nodes, nil
}

// errStopIteration is used to break out of iteration early.
var errStopIteration = fmt.Errorf("stop iteration")

// GetNodesByLevel returns all nodes at a given level.
func (s *Store) GetNodesByLevel(level types.HierarchyLevel) ([]*types.Node, error) {
	var nodes []*types.Node

	err := s.IterateNodes(func(node *types.Node) error {
		if node.Level == level {
			nodes = append(nodes, node)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}

	return nodes, nil
}

// Snapshot creates a snapshot of the database.
func (s *Store) Snapshot() *pebble.Snapshot {
	return s.db.NewSnapshot()
}

// Compact triggers compaction of the database.
func (s *Store) Compact() error {
	iter, err := s.db.NewIter(nil)
	if err != nil {
		return err
	}
	defer iter.Close()

	var first, last []byte
	if iter.First() {
		first = bytes.Clone(iter.Key())
	}
	if iter.Last() {
		last = bytes.Clone(iter.Key())
	}

	if first != nil && last != nil {
		return s.db.Compact(first, last, true)
	}

	return nil
}

// Stats returns storage statistics.
func (s *Store) Stats() map[string]interface{} {
	metrics := s.db.Metrics()
	return map[string]interface{}{
		"node_count":    s.nodeCounter.Load(),
		"sequence":      s.seqCounter.Load(),
		"disk_space":    metrics.DiskSpaceUsage(),
		"read_amp":      metrics.ReadAmp(),
		"flush_count":   metrics.Flush.Count,
		"compact_count": metrics.Compact.Count,
	}
}

// Flush ensures all data is written to disk.
func (s *Store) Flush() error {
	_, err := s.db.AsyncFlush()
	if err != nil {
		return types.WrapError("storage.Flush", types.ErrStorageIO, err)
	}

	return s.saveCounters()
}

// Debug helper to print storage stats
func (s *Store) DebugPrint() {
	stats := s.Stats()
	fmt.Printf("Storage Stats:\n")
	for k, v := range stats {
		fmt.Printf("  %s: %v\n", k, v)
	}
}
