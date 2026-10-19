#!/usr/bin/env bash
set -e

# Change to the repository root
cd "$(dirname "$0")/.."

# Configure CMake if build directory does not exist
if [ ! -d "build" ]; then
    cmake -B build -G Ninja -DCMAKE_CXX_COMPILER="clang++" 1>&2
fi

# Incrementally build the challenge_tests target, keeping compiler output on stderr
cmake --build build --target challenge_tests 1>&2

# Execute the test binary, redirecting stdout/stderr as appropriate and preserving exit code
set +e
if [ -f "./build/challenge_tests.exe" ]; then
    ./build/challenge_tests.exe
    TEST_EXIT=$?
else
    ./build/challenge_tests
    TEST_EXIT=$?
fi

exit $TEST_EXIT
