#!/bin/sh
# Builds and runs the host-side solver test (no game or NDK needed)
set -e
cd "$(dirname "$0")/.."
c++ -std=c++20 -O2 -DEASYOFFSET_HOST_TEST -Iinclude test/solver_test.cpp src/CalibrationSolver.cpp src/Analysis.cpp -o build/solver_test
./build/solver_test
