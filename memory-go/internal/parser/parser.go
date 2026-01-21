// Package parser provides content parsing for the hierarchical memory structure.
// It breaks messages into blocks (code blocks, paragraphs) and statements (sentences, code lines).
package parser

import (
	"regexp"
	"strings"
)

// Block represents a parsed block of content (code block, paragraph, etc.)
type Block struct {
	Content    string
	Type       BlockType
	Language   string // For code blocks
	Statements []Statement
}

// Statement represents a parsed statement (sentence, code line)
type Statement struct {
	Content string
	Type    StatementType
}

// BlockType identifies the type of block
type BlockType int

const (
	BlockParagraph BlockType = iota
	BlockCode
	BlockToolOutput
	BlockList
)

// StatementType identifies the type of statement
type StatementType int

const (
	StatementSentence StatementType = iota
	StatementCodeLine
	StatementListItem
)

// ParsedContent represents fully parsed message content
type ParsedContent struct {
	Blocks []Block
}

var (
	// Match fenced code blocks: ```lang\ncode\n```
	codeBlockRegex = regexp.MustCompile("(?s)```(\\w*)\\n?(.*?)```")
	// Match sentences (ending with . ! ? followed by space or end)
	sentenceRegex = regexp.MustCompile(`[^.!?]*[.!?](?:\s|$)`)
)

// Parse breaks message content into blocks and statements
func Parse(content string) *ParsedContent {
	result := &ParsedContent{
		Blocks: make([]Block, 0),
	}

	// Find all code blocks first
	codeMatches := codeBlockRegex.FindAllStringSubmatchIndex(content, -1)

	lastEnd := 0
	for _, match := range codeMatches {
		// Text before this code block
		if match[0] > lastEnd {
			textBefore := content[lastEnd:match[0]]
			blocks := parseTextBlocks(textBefore)
			result.Blocks = append(result.Blocks, blocks...)
		}

		// The code block itself
		lang := ""
		if match[2] != -1 && match[3] != -1 {
			lang = content[match[2]:match[3]]
		}
		codeContent := ""
		if match[4] != -1 && match[5] != -1 {
			codeContent = content[match[4]:match[5]]
		}

		codeBlock := Block{
			Content:    codeContent,
			Type:       BlockCode,
			Language:   lang,
			Statements: parseCodeStatements(codeContent),
		}
		result.Blocks = append(result.Blocks, codeBlock)

		lastEnd = match[1]
	}

	// Text after the last code block
	if lastEnd < len(content) {
		textAfter := content[lastEnd:]
		blocks := parseTextBlocks(textAfter)
		result.Blocks = append(result.Blocks, blocks...)
	}

	// If no blocks were found, treat entire content as one paragraph
	if len(result.Blocks) == 0 && len(strings.TrimSpace(content)) > 0 {
		result.Blocks = append(result.Blocks, Block{
			Content:    content,
			Type:       BlockParagraph,
			Statements: parseTextStatements(content),
		})
	}

	return result
}

// parseTextBlocks splits non-code text into paragraphs and lists
func parseTextBlocks(text string) []Block {
	blocks := make([]Block, 0)

	// Split by double newlines (paragraph boundaries)
	paragraphs := splitParagraphs(text)

	for _, para := range paragraphs {
		para = strings.TrimSpace(para)
		if para == "" {
			continue
		}

		// Check if it's a list
		if isListBlock(para) {
			blocks = append(blocks, Block{
				Content:    para,
				Type:       BlockList,
				Statements: parseListStatements(para),
			})
		} else {
			blocks = append(blocks, Block{
				Content:    para,
				Type:       BlockParagraph,
				Statements: parseTextStatements(para),
			})
		}
	}

	return blocks
}

// splitParagraphs splits text by blank lines while preserving single newlines
func splitParagraphs(text string) []string {
	// Split on two or more consecutive newlines
	parts := regexp.MustCompile(`\n\s*\n`).Split(text, -1)
	return parts
}

// isListBlock checks if a paragraph is a list (starts with -, *, or numbered)
func isListBlock(text string) bool {
	lines := strings.Split(text, "\n")
	if len(lines) == 0 {
		return false
	}

	listMarkers := 0
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		if strings.HasPrefix(trimmed, "- ") ||
			strings.HasPrefix(trimmed, "* ") ||
			strings.HasPrefix(trimmed, "+ ") ||
			regexp.MustCompile(`^\d+\.\s`).MatchString(trimmed) {
			listMarkers++
		}
	}

	// Consider it a list if most non-empty lines are list items
	return listMarkers > 0 && listMarkers >= len(lines)/2
}

// parseTextStatements splits paragraph text into sentences
func parseTextStatements(text string) []Statement {
	statements := make([]Statement, 0)

	// Simple sentence splitting - split on . ! ? followed by space or newline
	// But preserve newlines within the content
	text = strings.TrimSpace(text)
	if text == "" {
		return statements
	}

	// Split into sentences
	sentences := splitSentences(text)
	for _, sent := range sentences {
		sent = strings.TrimSpace(sent)
		if sent != "" {
			statements = append(statements, Statement{
				Content: sent,
				Type:    StatementSentence,
			})
		}
	}

	// If no sentences found, treat entire text as one statement
	if len(statements) == 0 {
		statements = append(statements, Statement{
			Content: text,
			Type:    StatementSentence,
		})
	}

	return statements
}

// splitSentences splits text into sentences
func splitSentences(text string) []string {
	var sentences []string

	// Handle common abbreviations to avoid false splits
	// Replace common abbreviations temporarily
	replacements := map[string]string{
		"Mr.": "Mr\x00",
		"Mrs.": "Mrs\x00",
		"Dr.": "Dr\x00",
		"etc.": "etc\x00",
		"e.g.": "e\x00g\x00",
		"i.e.": "i\x00e\x00",
	}

	for old, new := range replacements {
		text = strings.ReplaceAll(text, old, new)
	}

	// Split on sentence endings
	parts := regexp.MustCompile(`([.!?]+)\s+`).Split(text, -1)

	// Restore abbreviations
	for i, part := range parts {
		for old, new := range replacements {
			parts[i] = strings.ReplaceAll(part, new, old)
		}
	}

	// Rebuild sentences with their punctuation
	matches := regexp.MustCompile(`([.!?]+)\s+`).FindAllString(text, -1)
	for i, part := range parts {
		if part == "" {
			continue
		}
		sent := part
		if i < len(matches) {
			sent += strings.TrimSpace(matches[i])
		}
		// Restore abbreviations in final sentence
		for old, new := range replacements {
			sent = strings.ReplaceAll(sent, new, old)
		}
		sentences = append(sentences, sent)
	}

	return sentences
}

// parseCodeStatements splits code into individual lines/statements
func parseCodeStatements(code string) []Statement {
	statements := make([]Statement, 0)

	lines := strings.Split(code, "\n")
	for _, line := range lines {
		// Keep empty lines as they may be significant in code
		// But trim trailing whitespace
		line = strings.TrimRight(line, " \t")
		statements = append(statements, Statement{
			Content: line,
			Type:    StatementCodeLine,
		})
	}

	return statements
}

// parseListStatements splits a list block into list items
func parseListStatements(text string) []Statement {
	statements := make([]Statement, 0)

	lines := strings.Split(text, "\n")
	var currentItem strings.Builder

	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}

		// Check if this is a new list item
		isNewItem := strings.HasPrefix(trimmed, "- ") ||
			strings.HasPrefix(trimmed, "* ") ||
			strings.HasPrefix(trimmed, "+ ") ||
			regexp.MustCompile(`^\d+\.\s`).MatchString(trimmed)

		if isNewItem {
			// Save previous item if any
			if currentItem.Len() > 0 {
				statements = append(statements, Statement{
					Content: strings.TrimSpace(currentItem.String()),
					Type:    StatementListItem,
				})
				currentItem.Reset()
			}
			currentItem.WriteString(line)
		} else {
			// Continuation of previous item
			if currentItem.Len() > 0 {
				currentItem.WriteString("\n")
			}
			currentItem.WriteString(line)
		}
	}

	// Don't forget the last item
	if currentItem.Len() > 0 {
		statements = append(statements, Statement{
			Content: strings.TrimSpace(currentItem.String()),
			Type:    StatementListItem,
		})
	}

	return statements
}
