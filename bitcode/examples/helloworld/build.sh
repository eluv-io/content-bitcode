#!/bin/bash
set -Eeuo pipefail

extra_flags=""
if test $# -eq 1; then
    extra_flags="$1"
fi

clang_bin=clang++
stdcpp_include_flag=""
shared_flags="-I../../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit"

uname_str=`uname`
if [ "$uname_str" = "Darwin" ]; then
    stdcpp_include_flag="-I$( cd "$( dirname "`xcodebuild -find-executable clang`" )/../include/c++/v1" && pwd )"
    project_path="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../.." && pwd )"
    ws_path="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../../.." && pwd )"
    toolchain_path="$( cd "$ws_path/elv-toolchain/dist/darwin"* && pwd )"
    clang_bin=$toolchain_path/bin/clang++
fi

$clang_bin -O0 -g $extra_flags $stdcpp_include_flag $shared_flags -c helloworld.cpp -o helloworld.bc
