#!/bin/bash
# Populate test database with a detailed hierarchy tree (100+ nodes)
#
# Creates a realistic test dataset with:
#   - 5 sessions (auth-system, db-layer, api-endpoints, testing, devops)
#   - 20 messages across sessions
#   - 80+ blocks with code and explanations
#   - Statements demonstrating 4-level hierarchy
#
# Usage:
#   ./mkdb.sh                              # Use default host
#   MEMORY_SERVICE_HOST=host:port ./mkdb.sh  # Use custom host

set -e  # Exit on error

HOST="${MEMORY_SERVICE_HOST:-localhost:8080}"
REQUEST_ID=1
ERRORS=0

# Check if service is running
check_service() {
    echo "Checking connection to $HOST..."
    if ! curl -s --connect-timeout 2 "http://$HOST/health" > /dev/null 2>&1; then
        echo "ERROR: Cannot connect to memory service at $HOST"
        echo ""
        echo "Make sure the service is running:"
        echo "  ./build/bin/memory-service"
        echo "  # or"
        echo "  docker compose up -d"
        exit 1
    fi
    echo "Connected."
    echo ""
}

store_message() {
    local session_id="$1"
    local agent_id="$2"
    local content="$3"

    result=$(curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
        \"jsonrpc\": \"2.0\",
        \"method\": \"store\",
        \"params\": {\"session_id\": \"$session_id\", \"agent_id\": \"$agent_id\", \"content\": \"$content\"},
        \"id\": $REQUEST_ID
    }" 2>&1) || { echo "ERROR: Failed to store message"; ERRORS=$((ERRORS + 1)); return; }
    REQUEST_ID=$((REQUEST_ID + 1))

    # Check for JSON-RPC error
    if echo "$result" | grep -q '"error"'; then
        echo "ERROR: $result" >&2
        ERRORS=$((ERRORS + 1))
        return
    fi

    # Extract message_id from response
    echo "$result" | grep -o '"message_id":[0-9]*' | grep -o '[0-9]*'
}

store_block() {
    local parent_id="$1"
    local content="$2"

    result=$(curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
        \"jsonrpc\": \"2.0\",
        \"method\": \"store_block\",
        \"params\": {\"parent_id\": $parent_id, \"content\": \"$content\"},
        \"id\": $REQUEST_ID
    }" 2>&1) || { echo "ERROR: Failed to store block"; ERRORS=$((ERRORS + 1)); return; }
    REQUEST_ID=$((REQUEST_ID + 1))

    if echo "$result" | grep -q '"error"'; then
        echo "ERROR: $result" >&2
        ERRORS=$((ERRORS + 1))
        return
    fi

    echo "$result" | grep -o '"block_id":[0-9]*' | grep -o '[0-9]*'
}

store_statement() {
    local parent_id="$1"
    local content="$2"

    result=$(curl -s --fail -X POST "http://$HOST/rpc" -H "Content-Type: application/json" -d "{
        \"jsonrpc\": \"2.0\",
        \"method\": \"store_statement\",
        \"params\": {\"parent_id\": $parent_id, \"content\": \"$content\"},
        \"id\": $REQUEST_ID
    }" 2>&1) || { echo "ERROR: Failed to store statement"; ERRORS=$((ERRORS + 1)); return; }
    REQUEST_ID=$((REQUEST_ID + 1))

    if echo "$result" | grep -q '"error"'; then
        echo "ERROR: $result" >&2
        ERRORS=$((ERRORS + 1))
        return
    fi

    echo "$result" | grep -o '"statement_id":[0-9]*' | grep -o '[0-9]*'
}

# Verify service is running before proceeding
check_service

echo "Creating detailed hierarchy tree at $HOST..."
echo ""

# ==================== SESSION 1: Authentication System ====================
echo "Creating Session 1: Authentication System..."

msg1=$(store_message "auth-system" "coding-agent" "Implementing JWT authentication system for the API")
blk1=$(store_block "$msg1" "First, let's create the JWT token generation utility")
store_statement "$blk1" "Import the jsonwebtoken library"
store_statement "$blk1" "Define the generateToken function"
store_statement "$blk1" "Return signed JWT with userId payload"
blk2=$(store_block "$msg1" "function generateToken(userId, secret) { return jwt.sign({ userId }, secret, { expiresIn: '24h' }); }")
store_statement "$blk2" "Function takes userId and secret as parameters"
store_statement "$blk2" "Uses jwt.sign to create the token"
store_statement "$blk2" "Token expires in 24 hours"
store_block "$msg1" "The token includes the user ID and expires in 24 hours"
store_block "$msg1" "We use HS256 algorithm for signing"

msg2=$(store_message "auth-system" "coding-agent" "Adding token verification middleware")
store_block "$msg2" "const verifyToken = (req, res, next) => { const token = req.headers.authorization; }"
store_block "$msg2" "Extract Bearer token from Authorization header"
store_block "$msg2" "Verify token signature and expiration"
store_block "$msg2" "Attach decoded user info to request object"

msg3=$(store_message "auth-system" "coding-agent" "Implementing refresh token mechanism")
store_block "$msg3" "Refresh tokens allow obtaining new access tokens without re-authentication"
store_block "$msg3" "Store refresh tokens in database with expiration timestamp"
store_block "$msg3" "function refreshAccessToken(refreshToken) { /* validate and issue new token */ }"
store_block "$msg3" "Invalidate old refresh token after use for security"

msg4=$(store_message "auth-system" "coding-agent" "Adding password hashing with bcrypt")
store_block "$msg4" "const hashPassword = async (password) => bcrypt.hash(password, 10);"
store_block "$msg4" "Use cost factor of 10 for reasonable security/performance balance"
store_block "$msg4" "const verifyPassword = async (password, hash) => bcrypt.compare(password, hash);"

# ==================== SESSION 2: Database Layer ====================
echo "Creating Session 2: Database Layer..."

msg5=$(store_message "db-layer" "coding-agent" "Setting up PostgreSQL connection pool")
store_block "$msg5" "Using pg-pool for connection management"
store_block "$msg5" "const pool = new Pool({ max: 20, idleTimeoutMillis: 30000 });"
store_block "$msg5" "Connection pool prevents creating new connections for each query"
store_block "$msg5" "Maximum 20 concurrent connections to avoid overwhelming the database"

msg6=$(store_message "db-layer" "coding-agent" "Creating database migrations system")
store_block "$msg6" "Migrations track schema changes over time"
store_block "$msg6" "CREATE TABLE migrations (id SERIAL, name VARCHAR, applied_at TIMESTAMP);"
store_block "$msg6" "Each migration has up() and down() methods for apply/rollback"
store_block "$msg6" "Run migrations in order based on timestamp prefix"

msg7=$(store_message "db-layer" "coding-agent" "Implementing query builder for safe SQL construction")
store_block "$msg7" "Query builder prevents SQL injection by using parameterized queries"
store_block "$msg7" "class QueryBuilder { select(columns) { this.columns = columns; return this; } }"
store_block "$msg7" "Chain methods: select().from().where().orderBy().limit()"
store_block "$msg7" "Build final SQL with properly escaped parameters"

msg8=$(store_message "db-layer" "coding-agent" "Adding database transaction support")
store_block "$msg8" "Transactions ensure atomicity of multiple operations"
store_block "$msg8" "BEGIN; UPDATE accounts SET balance = balance - 100; UPDATE accounts SET balance = balance + 100; COMMIT;"
store_block "$msg8" "Rollback on any error to maintain data consistency"

# ==================== SESSION 3: API Endpoints ====================
echo "Creating Session 3: API Endpoints..."

msg9=$(store_message "api-endpoints" "coding-agent" "Creating RESTful user management endpoints")
store_block "$msg9" "GET /api/users - List all users with pagination"
store_block "$msg9" "POST /api/users - Create new user account"
store_block "$msg9" "GET /api/users/:id - Get user by ID"
store_block "$msg9" "PUT /api/users/:id - Update user information"
store_block "$msg9" "DELETE /api/users/:id - Soft delete user account"

msg10=$(store_message "api-endpoints" "coding-agent" "Implementing request validation middleware")
store_block "$msg10" "const validateRequest = (schema) => (req, res, next) => { schema.validate(req.body); }"
store_block "$msg10" "Use Joi or Zod for schema definition and validation"
store_block "$msg10" "Return 400 Bad Request with validation errors"
store_block "$msg10" "Sanitize input to prevent XSS attacks"

msg11=$(store_message "api-endpoints" "coding-agent" "Adding rate limiting to prevent abuse")
store_block "$msg11" "Rate limit: 100 requests per minute per IP address"
store_block "$msg11" "Use Redis to track request counts across server instances"
store_block "$msg11" "Return 429 Too Many Requests when limit exceeded"
store_block "$msg11" "Include Retry-After header in response"

msg12=$(store_message "api-endpoints" "coding-agent" "Implementing API versioning strategy")
store_block "$msg12" "URL path versioning: /api/v1/users, /api/v2/users"
store_block "$msg12" "Version 2 adds new fields while maintaining backward compatibility"
store_block "$msg12" "Deprecation warnings in response headers for old versions"

# ==================== SESSION 4: Testing Framework ====================
echo "Creating Session 4: Testing Framework..."

msg13=$(store_message "testing" "coding-agent" "Setting up Jest testing framework")
store_block "$msg13" "npm install --save-dev jest @types/jest ts-jest"
store_block "$msg13" "Configure jest.config.js with TypeScript preset"
store_block "$msg13" "Add test scripts to package.json: test, test:watch, test:coverage"
store_block "$msg13" "Set coverage threshold to 80% for all metrics"

msg14=$(store_message "testing" "coding-agent" "Writing unit tests for authentication module")
store_block "$msg14" "describe('JWT Token Generation', () => { test('should generate valid token', () => { }); });"
store_block "$msg14" "Test token generation with valid and invalid inputs"
store_block "$msg14" "Verify token expiration is set correctly"
store_block "$msg14" "Test token verification with expired and tampered tokens"

msg15=$(store_message "testing" "coding-agent" "Creating integration tests for API endpoints")
store_block "$msg15" "Use supertest for HTTP endpoint testing"
store_block "$msg15" "Set up test database with fixtures before each test suite"
store_block "$msg15" "Test complete user registration and login flow"
store_block "$msg15" "Verify proper error responses for edge cases"

msg16=$(store_message "testing" "coding-agent" "Adding mock implementations for external services")
store_block "$msg16" "jest.mock('./emailService') to mock email sending"
store_block "$msg16" "Create mock implementations that track calls and arguments"
store_block "$msg16" "Verify correct service interactions without side effects"

# ==================== SESSION 5: DevOps and Deployment ====================
echo "Creating Session 5: DevOps and Deployment..."

msg17=$(store_message "devops" "coding-agent" "Creating Dockerfile for containerization")
store_block "$msg17" "FROM node:18-alpine AS builder"
store_block "$msg17" "Multi-stage build to minimize final image size"
store_block "$msg17" "COPY package*.json ./ && RUN npm ci --only=production"
store_block "$msg17" "Final image is under 100MB with only production dependencies"

msg18=$(store_message "devops" "coding-agent" "Setting up GitHub Actions CI/CD pipeline")
store_block "$msg18" "Trigger on push to main branch and pull requests"
store_block "$msg18" "Run linting, type checking, and tests in parallel"
store_block "$msg18" "Build and push Docker image on successful merge to main"
store_block "$msg18" "Deploy to staging environment automatically"

msg19=$(store_message "devops" "coding-agent" "Configuring Kubernetes deployment")
store_block "$msg19" "apiVersion: apps/v1, kind: Deployment with 3 replicas"
store_block "$msg19" "Resource limits: 256Mi memory, 100m CPU per pod"
store_block "$msg19" "Horizontal Pod Autoscaler scales based on CPU utilization"
store_block "$msg19" "Rolling update strategy with maxSurge: 1, maxUnavailable: 0"

msg20=$(store_message "devops" "coding-agent" "Implementing health checks and monitoring")
store_block "$msg20" "GET /health endpoint returns service status and dependencies"
store_block "$msg20" "Liveness probe checks if process is running"
store_block "$msg20" "Readiness probe verifies database connection is active"
store_block "$msg20" "Prometheus metrics endpoint for custom application metrics"

echo ""
if [ $ERRORS -gt 0 ]; then
    echo "FAILED: $ERRORS errors occurred during data creation"
    exit 1
fi

echo "Done! Created hierarchical tree with:"
echo "  - 5 sessions"
echo "  - 20 messages"
echo "  - 80 blocks"
echo "  - 6 statements (demonstrating 4-level hierarchy)"
echo "  - Total: 111 nodes"
echo ""
echo "Try these queries to test:"
echo "  ./search.sh 'authentication'"
echo "  ./search.sh 'database' --level block"
echo "  ./drill_down.sh 0 --filter 'token'"
