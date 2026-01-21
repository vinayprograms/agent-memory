package session

import (
	"regexp"
	"strings"
	"unicode"
)

// Extractor extracts keywords, identifiers, and file paths from content.
type Extractor struct {
	// Common programming language keywords to ignore
	stopWords map[string]struct{}
	// Pattern for file paths
	filePathPattern *regexp.Regexp
	// Pattern for identifiers (camelCase, snake_case, etc.)
	identifierPattern *regexp.Regexp
}

// NewExtractor creates a new keyword extractor.
func NewExtractor() *Extractor {
	return &Extractor{
		stopWords: buildStopWords(),
		filePathPattern: regexp.MustCompile(
			`(?:^|[\s"'\(])(/[^\s"'\)]+|[a-zA-Z]:\\[^\s"'\)]+|\.{1,2}/[^\s"'\)]+)`,
		),
		identifierPattern: regexp.MustCompile(
			`\b([a-z][a-zA-Z0-9]*[A-Z][a-zA-Z0-9]*|[a-z]+_[a-z_0-9]+|[A-Z][a-z]+[A-Z][a-zA-Z0-9]*)\b`,
		),
	}
}

// buildStopWords returns common stop words to filter out.
func buildStopWords() map[string]struct{} {
	words := []string{
		// Common English
		"the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
		"have", "has", "had", "do", "does", "did", "will", "would", "could",
		"should", "may", "might", "must", "shall", "can", "need", "dare",
		"ought", "used", "to", "of", "in", "for", "on", "with", "at", "by",
		"from", "as", "into", "through", "during", "before", "after", "above",
		"below", "between", "under", "again", "further", "then", "once",
		"here", "there", "when", "where", "why", "how", "all", "each", "few",
		"more", "most", "other", "some", "such", "no", "nor", "not", "only",
		"own", "same", "so", "than", "too", "very", "just", "and", "but",
		"if", "or", "because", "until", "while", "this", "that", "these",
		"those", "it", "its",

		// Programming keywords
		"func", "function", "def", "class", "struct", "interface", "type",
		"var", "let", "const", "static", "public", "private", "protected",
		"return", "if", "else", "elif", "switch", "case", "default", "for",
		"while", "do", "break", "continue", "try", "catch", "except",
		"finally", "throw", "throws", "import", "from", "export", "package",
		"module", "require", "include", "using", "namespace", "new", "delete",
		"nil", "null", "none", "true", "false", "void", "int", "string",
		"bool", "float", "double", "char", "byte", "long", "short",
		"async", "await", "yield", "lambda", "self", "this", "super",
	}

	m := make(map[string]struct{}, len(words))
	for _, w := range words {
		m[w] = struct{}{}
	}
	return m
}

// Extract extracts keywords, identifiers, and file paths from content.
func (e *Extractor) Extract(content string) (keywords, identifiers, files []string) {
	// Extract file paths first
	files = e.extractFilePaths(content)

	// Extract identifiers
	identifiers = e.extractIdentifiers(content)

	// Extract keywords
	keywords = e.extractKeywords(content)

	return keywords, identifiers, files
}

// extractFilePaths extracts file paths from content.
func (e *Extractor) extractFilePaths(content string) []string {
	matches := e.filePathPattern.FindAllStringSubmatch(content, -1)
	seen := make(map[string]struct{})
	var files []string

	for _, match := range matches {
		if len(match) > 1 {
			path := strings.TrimSpace(match[1])
			// Filter out common false positives
			if isLikelyFilePath(path) {
				if _, exists := seen[path]; !exists {
					seen[path] = struct{}{}
					files = append(files, path)
				}
			}
		}
	}

	return files
}

// isLikelyFilePath checks if a string looks like a file path.
func isLikelyFilePath(s string) bool {
	// Must have at least one path separator
	if !strings.Contains(s, "/") && !strings.Contains(s, "\\") {
		return false
	}

	// Should have a file extension or be a directory reference
	parts := strings.Split(s, "/")
	if len(parts) == 0 {
		parts = strings.Split(s, "\\")
	}

	lastPart := parts[len(parts)-1]

	// Check for file extension
	if strings.Contains(lastPart, ".") {
		return true
	}

	// Check for common directories
	commonDirs := []string{"src", "lib", "bin", "pkg", "cmd", "internal", "test", "tests", "docs"}
	for _, dir := range commonDirs {
		if strings.Contains(strings.ToLower(s), dir) {
			return true
		}
	}

	return len(s) > 3 && len(parts) > 1
}

// extractIdentifiers extracts programming identifiers.
func (e *Extractor) extractIdentifiers(content string) []string {
	matches := e.identifierPattern.FindAllString(content, -1)
	seen := make(map[string]struct{})
	var identifiers []string

	for _, match := range matches {
		if len(match) >= 4 { // Minimum length for meaningful identifier
			lower := strings.ToLower(match)
			if _, isStop := e.stopWords[lower]; !isStop {
				if _, exists := seen[match]; !exists {
					seen[match] = struct{}{}
					identifiers = append(identifiers, match)
				}
			}
		}
	}

	return identifiers
}

// extractKeywords extracts significant keywords from content.
func (e *Extractor) extractKeywords(content string) []string {
	// Tokenize
	words := tokenizeForKeywords(content)

	// Count frequency
	freq := make(map[string]int)
	for _, word := range words {
		lower := strings.ToLower(word)
		if _, isStop := e.stopWords[lower]; !isStop {
			if len(word) >= 3 { // Minimum length
				freq[lower]++
			}
		}
	}

	// Sort by frequency and take top keywords
	type wordFreq struct {
		word  string
		count int
	}
	var sorted []wordFreq
	for w, c := range freq {
		sorted = append(sorted, wordFreq{w, c})
	}

	// Simple insertion sort by count descending
	for i := 1; i < len(sorted); i++ {
		j := i
		for j > 0 && sorted[j].count > sorted[j-1].count {
			sorted[j], sorted[j-1] = sorted[j-1], sorted[j]
			j--
		}
	}

	// Take top keywords
	var keywords []string
	for i := 0; i < len(sorted) && i < 20; i++ {
		keywords = append(keywords, sorted[i].word)
	}

	return keywords
}

// tokenizeForKeywords splits content into word tokens.
func tokenizeForKeywords(content string) []string {
	var words []string
	var current strings.Builder

	for _, r := range content {
		if unicode.IsLetter(r) || unicode.IsDigit(r) {
			current.WriteRune(r)
		} else {
			if current.Len() > 0 {
				words = append(words, current.String())
				current.Reset()
			}
		}
	}

	if current.Len() > 0 {
		words = append(words, current.String())
	}

	return words
}

// ExtractFromMultiple extracts and merges from multiple content strings.
func (e *Extractor) ExtractFromMultiple(contents []string) (keywords, identifiers, files []string) {
	keywordSet := make(map[string]int)
	identifierSet := make(map[string]struct{})
	fileSet := make(map[string]struct{})

	for _, content := range contents {
		kw, ids, fs := e.Extract(content)

		for _, k := range kw {
			keywordSet[k]++
		}
		for _, id := range ids {
			identifierSet[id] = struct{}{}
		}
		for _, f := range fs {
			fileSet[f] = struct{}{}
		}
	}

	// Convert sets to slices
	for k := range keywordSet {
		keywords = append(keywords, k)
	}
	for id := range identifierSet {
		identifiers = append(identifiers, id)
	}
	for f := range fileSet {
		files = append(files, f)
	}

	return keywords, identifiers, files
}
