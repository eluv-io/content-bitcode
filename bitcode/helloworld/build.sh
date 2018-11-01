#!/bin/bash
set -Eeuo pipefail

# TODO
#   - Add script argument to select DEBUG or RELEASE, i.e. -g or not
#   - Move compilation command into a function

clang_bin=clang++

shared_flags="-I../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit"

$clang_bin -O0 -g $shared_flags -c helloworld.cpp -o helloworld.bc

