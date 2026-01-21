#!/bin/bash
# Search memories by query string with optional level filtering
#
# Usage:
#   ./search.sh <query>                     # Search all levels
#   ./search.sh <query> --level block       # Search only blocks
#   ./search.sh <query> --top message       # Search message down to statement
#   ./search.sh <query> --bottom block      # Search session down to block
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
    echo "Usage: $0 <query> [options]"
    echo ""
    echo "Options:"
    echo "  --level <level>    Search only this level (session|message|block|statement)"
    echo "  --top <level>      Highest level to search (default: session)"
    echo "  --bottom <level>   Lowest level to search (default: statement)"
    echo "  --max <n>          Maximum results (default: 10, max: 100)"
    echo ""
    echo "Examples:"
    echo "  $0 'authentication'                    # Search all levels"
    echo "  $0 'database' --level block            # Search only blocks"
    echo "  $0 'JWT token' --top message           # Skip session level"
    echo "  $0 'API endpoint' --bottom block       # Skip statement level"
    exit 1
}

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

QUERY="$1"
shift

# Parse optional arguments
LEVEL=""
TOP_LEVEL=""
BOTTOM_LEVEL=""
MAX_RESULTS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --level)
            LEVEL="$2"
            shift 2
            ;;
        --top)
            TOP_LEVEL="$2"
            shift 2
            ;;
        --bottom)
            BOTTOM_LEVEL="$2"
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
PARAMS="\"query\": \"$QUERY\""

if [ -n "$LEVEL" ]; then
    PARAMS="$PARAMS, \"level\": \"$LEVEL\""
fi

if [ -n "$TOP_LEVEL" ]; then
    PARAMS="$PARAMS, \"top_level\": \"$TOP_LEVEL\""
fi

if [ -n "$BOTTOM_LEVEL" ]; then
    PARAMS="$PARAMS, \"bottom_level\": \"$BOTTOM_LEVEL\""
fi

if [ -n "$MAX_RESULTS" ]; then
    PARAMS="$PARAMS, \"max_results\": $MAX_RESULTS"
fi

check_service
curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
  \"jsonrpc\": \"2.0\",
  \"method\": \"query\",
  \"params\": {$PARAMS},
  \"id\": 1
}" | python3 -m json.tool 2>/dev/null || cat
echo ""
