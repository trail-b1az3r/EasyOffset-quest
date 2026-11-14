#!/bin/sh
# Configure + build. Needs ndkpath.txt (or ANDROID_NDK_HOME), cmake and ninja.
set -e
cd "$(dirname "$0")/.."
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build
cmake --build build
