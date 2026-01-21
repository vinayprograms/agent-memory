#!/bin/bash
# Push messages to memory-service
# Reads JSON from stdin, extracts content, sends to memory-service
#
# Set AGENT_ID environment variable when starting Claude Code:
#   AGENT_ID=my-agent claude

set -e

MEMORY_HOST="${MEMORY_SERVICE_HOST:-localhost:8080}"

# Read stdin ONCE into a variable
INPUT=$(cat)

# Agent ID from environment (required for multi-instance separation)
AGENT="${AGENT_ID:-default}"

SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // "unknown"')
EVENT=$(echo "$INPUT" | jq -r '.hook_event_name')

case "$EVENT" in
  UserPromptSubmit)
    CONTENT=$(echo "$INPUT" | jq -r '.prompt // empty')
    ROLE="user"
    ;;
  Stop)
    # Stop event doesn't include response - find last assistant message with text from transcript
    TRANSCRIPT=$(echo "$INPUT" | jq -r '.transcript_path // empty')
    if [ -n "$TRANSCRIPT" ] && [ -f "$TRANSCRIPT" ]; then
      # Find the last assistant entry that has text content (not just tool_use)
      CONTENT=$(grep '"type":"assistant"' "$TRANSCRIPT" | tail -1 | jq -r '.message.content[]? | select(.type == "text") | .text' 2>/dev/null | tr '\n' ' ')
    fi
    ROLE="assistant"
    ;;
  PostToolUse)
    TOOL=$(echo "$INPUT" | jq -r '.tool_name')
    ROLE="tool"

    case "$TOOL" in
      Edit)
        # Extract file path and diff lines only (not originalFile)
        FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // "unknown"')
        DIFF=$(echo "$INPUT" | jq -r '.tool_response.structuredPatch[]?.lines[]? // empty' 2>/dev/null | head -50)
        CONTENT="Edit: $FILE
$DIFF"
        ;;
      Bash)
        # Use description if available, otherwise truncate command
        DESC=$(echo "$INPUT" | jq -r '.tool_input.description // empty')
        CMD=$(echo "$INPUT" | jq -r '.tool_input.command // empty' | head -c 300)
        STDOUT=$(echo "$INPUT" | jq -r '.tool_response.stdout // empty' | head -c 500)
        if [ -n "$DESC" ]; then
          CONTENT="Bash: $DESC
$ $CMD
$STDOUT"
        else
          CONTENT="Bash: $CMD
$STDOUT"
        fi
        ;;
      Read)
        # Just file path and brief preview
        FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // "unknown"')
        PREVIEW=$(echo "$INPUT" | jq -r '.tool_response.content // .tool_response.file.content // empty' 2>/dev/null | head -c 500)
        CONTENT="Read: $FILE
$PREVIEW"
        ;;
      Write)
        FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // "unknown"')
        PREVIEW=$(echo "$INPUT" | jq -r '.tool_input.content // empty' | head -c 500)
        CONTENT="Write: $FILE
$PREVIEW"
        ;;
      Grep|Glob)
        PATTERN=$(echo "$INPUT" | jq -r '.tool_input.pattern // empty')
        MATCHES=$(echo "$INPUT" | jq -r '.tool_response // empty' | head -c 500)
        CONTENT="$TOOL: $PATTERN
$MATCHES"
        ;;
      *)
        # Generic fallback - truncate everything
        INPUT_DATA=$(echo "$INPUT" | jq -c '.tool_input' | head -c 500)
        RESULT=$(echo "$INPUT" | jq -c '.tool_response // {}' | head -c 500)
        CONTENT="Tool: $TOOL
Input: $INPUT_DATA
Result: $RESULT"
        ;;
    esac
    ;;
  *)
    exit 0
    ;;
esac

if [ -n "$CONTENT" ]; then
  curl -s -X POST "http://$MEMORY_HOST/rpc" \
    -H "Content-Type: application/json" \
    -d "$(jq -n --arg agent "$AGENT" --arg sid "$SESSION_ID" --arg role "$ROLE" --arg content "$CONTENT" \
      '{jsonrpc:"2.0",method:"store",params:{agent_id:$agent,session_id:$sid,role:$role,content:$content},id:1}')" \
    >/dev/null 2>&1
fi

exit 0
