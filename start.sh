#!/usr/bin/env bash
set -e

# 1. Change to the repository root
cd "$(dirname "$0")"

# Configure port
export PORT="${PORT:-8080}"

# 2. Configure CMake if build directory does not exist
if [ ! -d "build" ]; then
    echo "[start.sh] Initializing CMake configuration..."
    cmake -B build -G Ninja -DCMAKE_CXX_COMPILER="clang++"
fi

# 3. Build server target
echo "[start.sh] Building server target..."
cmake --build build --target server

SERVER_PID=""

# Function to start the server
start_server() {
    echo "[start.sh] Launching BuildMesh server on port $PORT..."
    if [ -f "./build/server.exe" ]; then
        ./build/server.exe &
        SERVER_PID=$!
    else
        ./build/server &
        SERVER_PID=$!
    fi
    echo "[start.sh] Server started with PID $SERVER_PID"
}

# Function to stop the server
stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[start.sh] Stopping server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

# Cleanup on exit or signals
cleanup() {
    echo "[start.sh] Caught termination signal. Cleaning up..."
    stop_server
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

# 4. Launch server initially
start_server

# 7. File watcher for hot reload
echo "[start.sh] Watching src/ and include/ for candidate modifications..."

compute_checksum() {
    find src include -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -exec ls -l --time-style=+%s {} + 2>/dev/null | sort | md5sum | awk '{print $1}'
}

LAST_CHECKSUM=$(compute_checksum)

while true; do
    sleep 2
    CURRENT_CHECKSUM=$(compute_checksum)

    if [ "$CURRENT_CHECKSUM" != "$LAST_CHECKSUM" ]; then
        echo "[start.sh] Detected code modifications. Rebuilding server target..."
        LAST_CHECKSUM="$CURRENT_CHECKSUM"

        if cmake --build build --target server; then
            echo "[start.sh] Build succeeded! Restarting server..."
            stop_server
            start_server
        else
            echo "[start.sh] Build failed! Preserving running server instance."
        fi
    fi
done
