#!/bin/bash

# Integration test script for Web2D Social Game Engine

set -e

echo "Web2D Social Game Engine - Integration Test"
echo "==========================================="
echo ""

# Build backend
echo "Step 1: Building backend..."
cd backend
bash build.sh
cd ..
echo "✓ Backend built successfully"
echo ""

# Test backend
echo "Step 2: Testing backend..."
cd backend/build
mkdir -p db

# Start server in background
./game_server 8080 &
SERVER_PID=$!
sleep 2

# Test API endpoints
echo "Testing API endpoint..."
RESPONSE=$(curl -s http://localhost:8080/)
if echo "$RESPONSE" | grep -q "Web2D Game Server"; then
    echo "✓ Backend API is responding"
else
    echo "✗ Backend API test failed"
    kill $SERVER_PID 2>/dev/null || true
    exit 1
fi

# Test POST endpoint
echo "Testing POST endpoint..."
curl -s -X POST http://localhost:8080/api/game_state \
    -H "Content-Type: application/json" \
    -d '{"player":{"x":100,"y":200},"score":50}' > /dev/null
echo "✓ POST endpoint working"

# Stop server
kill $SERVER_PID 2>/dev/null || true
cd ../..
echo ""

# Test frontend
echo "Step 3: Testing frontend..."
cd frontend

# Check if all files exist
FILES=("index.html" "src/api-client.js" "src/game.js" "src/main.js" "src/styles.css")
for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "✗ Missing file: $file"
        exit 1
    fi
done
echo "✓ All frontend files present"

# Start HTTP server in background
python3 -m http.server 3000 &
FRONTEND_PID=$!
sleep 2

# Test frontend
echo "Testing frontend server..."
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:3000/)
if [ "$HTTP_CODE" = "200" ]; then
    echo "✓ Frontend server is running"
else
    echo "✗ Frontend server test failed (HTTP $HTTP_CODE)"
    kill $FRONTEND_PID 2>/dev/null || true
    exit 1
fi

# Stop frontend server
kill $FRONTEND_PID 2>/dev/null || true
cd ..
echo ""

echo "==========================================="
echo "✓ All integration tests passed!"
echo ""
echo "To run the application:"
echo "  1. Start backend:  cd backend/build && ./game_server 8080"
echo "  2. Start frontend: cd frontend && python3 -m http.server 8000"
echo "  3. Open browser:   http://localhost:8000"
echo ""
