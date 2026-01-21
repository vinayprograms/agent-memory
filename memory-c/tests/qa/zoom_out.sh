#!/bin/bash
# Zoom out from a node to see its context (ancestors)
#
# Usage:
#   ./zoom_out.sh <node_id>
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
    echo "Usage: $0 <node_id>"
    echo ""
    echo "Returns the node and its ancestor chain up to session level."
    echo ""
    echo "Examples:"
    echo "  $0 15    # Get ancestors of node 15"
    exit 1
}

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
fi

NODE_ID="$1"

check_service
curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
  \"jsonrpc\": \"2.0\",
  \"method\": \"zoom_out\",
  \"params\": {\"id\": $NODE_ID},
  \"id\": 1
}" | python3 -m json.tool 2>/dev/null || cat
echo ""
