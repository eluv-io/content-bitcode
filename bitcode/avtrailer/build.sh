#!/bin/bash
set -Eeuxo pipefail

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $dir

#clang++ -O0 -I../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit \
clang++ -O0 -I../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit \
-c avtrailer.cpp -o avtrailer.bc
