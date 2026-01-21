#!/bin/bash
# Drill down into a node to see its children with optional text filter
#
# Usage:
#   ./drill_down.sh <node_id>                    # Get all children
#   ./drill_down.sh <node_id> --filter "error"   # Filter children by text
#   ./drill_down.sh <node_id> --max 5            # Limit results
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
    echo "Usage: $0 <node_id> [options]"
    echo ""
    echo "Options:"
    echo "  --filter <text>    Filter children containing this text (case-insensitive)"
    echo "  --max <n>          Maximum children to return (default: 100)"
    echo ""
    echo "Examples:"
    echo "  $0 0                          # Get all children of node 0"
    echo "  $0 5 --filter 'authentication' # Filter children containing 'authentication'"
    echo "  $0 2 --max 10                 # Get first 10 children"
    exit 1
}

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

NODE_ID="$1"
shift

# Parse optional arguments
FILTER=""
MAX_RESULTS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --filter)
            FILTER="$2"
            shift 2
            ;;
        --max)
            MAX_RESULTS="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Build params JSON
PARAMS="\"id\": $NODE_ID"

if [ -n "$FILTER" ]; then
    PARAMS="$PARAMS, \"filter\": \"$FILTER\""
fi

if [ -n "$MAX_RESULTS" ]; then
    PARAMS="$PARAMS, \"max_results\": $MAX_RESULTS"
fi

check_service
curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
  \"jsonrpc\": \"2.0\",
  \"method\": \"drill_down\",
  \"params\": {$PARAMS},
  \"id\": 1
}" | python3 -m json.tool 2>/dev/null || cat
echo ""
