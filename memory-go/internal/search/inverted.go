package search

import (
	"math"
	"strings"
	"sync"
	"unicode"

	"github.com/anthropics/memory-go/pkg/types"
)

// BM25 parameters (matching C implementation)
const (
	BM25K1 = 1.2  // Term saturation parameter
	BM25B  = 0.75 // Length normalization parameter
)

// InvertedIndex provides keyword-based search using an inverted index with BM25 scoring.
type InvertedIndex struct {
	// token -> posting list (node ID -> term frequency)
	index map[string]map[types.NodeID]uint16
	// node ID -> document info (for BM25)
	docInfo map[types.NodeID]*docInfo
	// node ID -> tokens in that node (for deletion)
	nodeTokens map[types.NodeID][]string
	// Total number of documents
	docCount int
	// Sum of all document lengths
	totalDocLen int
	mu          sync.RWMutex
}

// docInfo stores document-level information for BM25 scoring.
type docInfo struct {
	length int // Number of tokens in document
}

// NewInvertedIndex creates a new inverted index.
func NewInvertedIndex() *InvertedIndex {
	return &InvertedIndex{
		index:      make(map[string]map[types.NodeID]uint16),
		docInfo:    make(map[types.NodeID]*docInfo),
		nodeTokens: make(map[types.NodeID][]string),
	}
}

// Add indexes a node's content.
func (ii *InvertedIndex) Add(id types.NodeID, content string) {
	tokens := tokenize(content)
	if len(tokens) == 0 {
		return
	}

	ii.mu.Lock()
	defer ii.mu.Unlock()

	// Remove old entry if exists
	if oldTokens, exists := ii.nodeTokens[id]; exists {
		ii.removeUnlocked(id, oldTokens)
	}

	// Count term frequencies
	termFreqs := make(map[string]uint16)
	for _, token := range tokens {
		termFreqs[token]++
	}

	// Add to index
	ii.nodeTokens[id] = tokens
	ii.docInfo[id] = &docInfo{length: len(tokens)}
	ii.docCount++
	ii.totalDocLen += len(tokens)

	for token, freq := range termFreqs {
		if ii.index[token] == nil {
			ii.index[token] = make(map[types.NodeID]uint16)
		}
		ii.index[token][id] = freq
	}
}

// Remove removes a node from the index.
func (ii *InvertedIndex) Remove(id types.NodeID) {
	ii.mu.Lock()
	defer ii.mu.Unlock()

	tokens, exists := ii.nodeTokens[id]
	if !exists {
		return
	}

	ii.removeUnlocked(id, tokens)
}

// removeUnlocked removes a node from the index (must hold write lock).
func (ii *InvertedIndex) removeUnlocked(id types.NodeID, tokens []string) {
	// Update document stats
	if info, ok := ii.docInfo[id]; ok {
		ii.totalDocLen -= info.length
		ii.docCount--
		delete(ii.docInfo, id)
	}

	// Remove from posting lists
	for _, token := range tokens {
		if nodeSet, ok := ii.index[token]; ok {
			delete(nodeSet, id)
			if len(nodeSet) == 0 {
				delete(ii.index, token)
			}
		}
	}

	delete(ii.nodeTokens, id)
}

// SearchAND finds nodes containing ALL query tokens.
func (ii *InvertedIndex) SearchAND(query string) []types.NodeID {
	tokens := tokenize(query)
	if len(tokens) == 0 {
		return nil
	}

	ii.mu.RLock()
	defer ii.mu.RUnlock()

	// Start with the first token's posting list
	firstToken := tokens[0]
	firstSet, ok := ii.index[firstToken]
	if !ok {
		return nil
	}

	// Intersect with remaining tokens
	result := make(map[types.NodeID]struct{})
	for id := range firstSet {
		result[id] = struct{}{}
	}

	for _, token := range tokens[1:] {
		nodeSet, ok := ii.index[token]
		if !ok {
			return nil // No nodes contain this token
		}

		// Intersect
		for id := range result {
			if _, exists := nodeSet[id]; !exists {
				delete(result, id)
			}
		}

		if len(result) == 0 {
			return nil
		}
	}

	// Convert to slice
	ids := make([]types.NodeID, 0, len(result))
	for id := range result {
		ids = append(ids, id)
	}

	return ids
}

// SearchOR finds nodes containing ANY query token.
func (ii *InvertedIndex) SearchOR(query string) []types.NodeID {
	tokens := tokenize(query)
	if len(tokens) == 0 {
		return nil
	}

	ii.mu.RLock()
	defer ii.mu.RUnlock()

	result := make(map[types.NodeID]struct{})

	for _, token := range tokens {
		if nodeSet, ok := ii.index[token]; ok {
			for id := range nodeSet {
				result[id] = struct{}{}
			}
		}
	}

	// Convert to slice
	ids := make([]types.NodeID, 0, len(result))
	for id := range result {
		ids = append(ids, id)
	}

	return ids
}

// SearchWithScores finds nodes and returns them with BM25 scores.
func (ii *InvertedIndex) SearchWithScores(query string, maxResults int) []KeywordMatch {
	tokens := tokenize(query)
	if len(tokens) == 0 {
		return nil
	}

	ii.mu.RLock()
	defer ii.mu.RUnlock()

	// Calculate average document length
	avgDocLen := 1.0
	if ii.docCount > 0 {
		avgDocLen = float64(ii.totalDocLen) / float64(ii.docCount)
	}

	// Calculate BM25 scores for each document
	scores := make(map[types.NodeID]float32)

	for _, token := range tokens {
		postingList, ok := ii.index[token]
		if !ok {
			continue
		}

		// Calculate IDF for this term
		// IDF = log((N - df + 0.5) / (df + 0.5))
		df := len(postingList)
		idf := math.Log((float64(ii.docCount)-float64(df)+0.5)/(float64(df)+0.5) + 1)

		// Calculate BM25 score contribution for each document
		for docID, tf := range postingList {
			docLen := 1.0
			if info, ok := ii.docInfo[docID]; ok {
				docLen = float64(info.length)
			}

			// BM25 formula:
			// score = IDF * (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * (docLen / avgDocLen)))
			tfFloat := float64(tf)
			denominator := tfFloat + BM25K1*(1-BM25B+BM25B*(docLen/avgDocLen))
			numerator := tfFloat * (BM25K1 + 1)
			score := idf * (numerator / denominator)

			scores[docID] += float32(score)
		}
	}

	// Convert to sorted slice
	results := make([]KeywordMatch, 0, len(scores))
	for id, score := range scores {
		results = append(results, KeywordMatch{
			NodeID: id,
			Score:  score,
		})
	}

	// Sort by score descending
	sortByScore(results)

	// Normalize scores to 0-1 range
	if len(results) > 0 {
		maxScore := results[0].Score
		if maxScore > 0 {
			for i := range results {
				results[i].Score /= maxScore
			}
		}
	}

	if len(results) > maxResults {
		results = results[:maxResults]
	}

	return results
}

// KeywordMatch represents a keyword search result.
type KeywordMatch struct {
	NodeID types.NodeID
	Score  float32
}

// sortByScore sorts matches by score (descending) using insertion sort.
func sortByScore(matches []KeywordMatch) {
	for i := 1; i < len(matches); i++ {
		j := i
		for j > 0 && matches[j].Score > matches[j-1].Score {
			matches[j], matches[j-1] = matches[j-1], matches[j]
			j--
		}
	}
}

// Contains checks if any node contains all query tokens.
func (ii *InvertedIndex) Contains(query string) bool {
	return len(ii.SearchAND(query)) > 0
}

// Size returns the number of unique tokens in the index.
func (ii *InvertedIndex) Size() int {
	ii.mu.RLock()
	defer ii.mu.RUnlock()
	return len(ii.index)
}

// NodeCount returns the number of indexed nodes.
func (ii *InvertedIndex) NodeCount() int {
	ii.mu.RLock()
	defer ii.mu.RUnlock()
	return len(ii.nodeTokens)
}

// Clear removes all entries from the index.
func (ii *InvertedIndex) Clear() {
	ii.mu.Lock()
	defer ii.mu.Unlock()
	ii.index = make(map[string]map[types.NodeID]uint16)
	ii.docInfo = make(map[types.NodeID]*docInfo)
	ii.nodeTokens = make(map[types.NodeID][]string)
	ii.docCount = 0
	ii.totalDocLen = 0
}

// Stats returns index statistics.
func (ii *InvertedIndex) Stats() map[string]interface{} {
	ii.mu.RLock()
	defer ii.mu.RUnlock()

	avgDocLen := 0.0
	if ii.docCount > 0 {
		avgDocLen = float64(ii.totalDocLen) / float64(ii.docCount)
	}

	return map[string]interface{}{
		"unique_tokens":  len(ii.index),
		"indexed_nodes":  len(ii.nodeTokens),
		"avg_doc_length": avgDocLen,
	}
}

// tokenize splits text into lowercase tokens.
func tokenize(text string) []string {
	text = strings.ToLower(text)

	var tokens []string
	var current strings.Builder

	for _, r := range text {
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '_' {
			current.WriteRune(r)
		} else {
			if current.Len() > 0 {
				token := current.String()
				if len(token) >= 2 && !isStopWord(token) { // Minimum token length + stop word filter
					tokens = append(tokens, token)
				}
				current.Reset()
			}
		}
	}

	// Don't forget the last token
	if current.Len() > 0 {
		token := current.String()
		if len(token) >= 2 && !isStopWord(token) {
			tokens = append(tokens, token)
		}
	}

	return tokens
}

// isStopWord checks if a token is a common stop word.
func isStopWord(token string) bool {
	stopWords := map[string]struct{}{
		// Common English stop words
		"the": {}, "is": {}, "at": {}, "which": {}, "on": {}, "a": {}, "an": {},
		"and": {}, "or": {}, "but": {}, "in": {}, "with": {}, "to": {}, "for": {},
		"of": {}, "this": {}, "that": {}, "it": {}, "as": {}, "be": {}, "by": {},
		"are": {}, "was": {}, "were": {}, "been": {}, "being": {}, "have": {},
		"has": {}, "had": {}, "do": {}, "does": {}, "did": {}, "will": {},
		"would": {}, "could": {}, "should": {}, "may": {}, "might": {},
		"must": {}, "can": {}, "if": {}, "then": {}, "else": {},
		// Common programming stop words
		"var": {}, "let": {}, "const": {}, "return": {}, "func": {},
	}
	_, isStop := stopWords[token]
	return isStop
}

// dedupe removes duplicate tokens while preserving order.
func dedupe(tokens []string) []string {
	seen := make(map[string]struct{})
	result := make([]string, 0, len(tokens))

	for _, token := range tokens {
		if _, exists := seen[token]; !exists {
			seen[token] = struct{}{}
			result = append(result, token)
		}
	}

	return result
}
