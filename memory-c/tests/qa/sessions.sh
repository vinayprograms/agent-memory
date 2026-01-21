#!/bin/bash
# List sessions or get a specific session
#
# Usage:
#   ./sessions.sh                    # List all sessions
#   ./sessions.sh <session_id>       # Get specific session details

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
    echo "Usage: $0 [session_id]"
    echo ""
    echo "  No args: List all sessions"
    echo "  <session_id>: Get details of specific session"
    echo ""
    echo "Examples:"
    echo "  $0                    # List all sessions"
    echo "  $0 auth-system        # Get 'auth-system' session"
    exit 1
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

check_service

if [ -z "$1" ]; then
    # List all sessions
    curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d '{
      "jsonrpc": "2.0",
      "method": "list_sessions",
      "params": {},
      "id": 1
    }' | python3 -m json.tool 2>/dev/null || cat
else
    # Get specific session
    SESSION_ID="$1"
    curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
      \"jsonrpc\": \"2.0\",
      \"method\": \"get_session\",
      \"params\": {\"session_id\": \"$SESSION_ID\"},
      \"id\": 1
    }" | python3 -m json.tool 2>/dev/null || cat
fi

echo ""
