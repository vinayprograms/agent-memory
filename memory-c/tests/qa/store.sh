#!/bin/bash
# Store content at any level of the hierarchy
#
# Usage:
#   ./store.sh message <session_id> <agent_id> <content>
#   ./store.sh block <parent_id> <content>
#   ./store.sh statement <parent_id> <content>
#
# Level hierarchy (top to bottom):
#   session -> message -> block -> statement

set -e
HOST="${MEMORY_SERVICE_HOST:-localhost:8080}"

# Check service connectivity
check_service() {
    if ! curl -s --connect-timeout 2 "http://$HOST/health" > /dev/null 2>&1; then
        echo "ERROR: Cannot connect to memory service at $HOST" >&2
        echo "Start the service with: ./build/bin/memory-service" >&2
        exit 1
    fi
}

usage() {
    echo "Usage: $0 <type> <args...>"
    echo ""
    echo "Types:"
    echo "  message <session_id> <agent_id> <content>"
    echo "      Store a message (creates session if needed)"
    echo ""
    echo "  block <parent_id> <content>"
    echo "      Store a block under a message"
    echo ""
    echo "  statement <parent_id> <content>"
    echo "      Store a statement under a block"
    echo ""
    echo "Examples:"
    echo "  $0 message 'my-session' 'coding-agent' 'Working on auth'"
    echo "  $0 block 5 'Here is the implementation code'"
    echo "  $0 statement 10 'Import the jwt library'"
    exit 1
}

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

TYPE="$1"
shift

check_service

case "$TYPE" in
    message)
        if [ $# -lt 3 ]; then
            echo "Error: message requires session_id, agent_id, and content"
            usage
        fi
        SESSION_ID="$1"
        AGENT_ID="$2"
        CONTENT="$3"

        curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
          \"jsonrpc\": \"2.0\",
          \"method\": \"store\",
          \"params\": {\"session_id\": \"$SESSION_ID\", \"agent_id\": \"$AGENT_ID\", \"content\": \"$CONTENT\"},
          \"id\": 1
        }" | python3 -m json.tool 2>/dev/null || cat
        ;;

    block)
        if [ $# -lt 2 ]; then
            echo "Error: block requires parent_id and content"
            usage
        fi
        PARENT_ID="$1"
        CONTENT="$2"

        curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
          \"jsonrpc\": \"2.0\",
          \"method\": \"store_block\",
          \"params\": {\"parent_id\": $PARENT_ID, \"content\": \"$CONTENT\"},
          \"id\": 1
        }" | python3 -m json.tool 2>/dev/null || cat
        ;;

    statement)
        if [ $# -lt 2 ]; then
            echo "Error: statement requires parent_id and content"
            usage
        fi
        PARENT_ID="$1"
        CONTENT="$2"

        curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
          \"jsonrpc\": \"2.0\",
          \"method\": \"store_statement\",
          \"params\": {\"parent_id\": $PARENT_ID, \"content\": \"$CONTENT\"},
          \"id\": 1
        }" | python3 -m json.tool 2>/dev/null || cat
        ;;

    *)
        echo "Unknown type: $TYPE"
        usage
        ;;
esac

echo ""
