#!/bin/bash
set -Eeuxo pipefail

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $dir

clang_bin=clang++
stdcpp_include_flag=""

uname_str=`uname`
if [ "$uname_str" = "Darwin" ]; then
    stdcpp_include_flag="-I$( cd "$( dirname "`xcodebuild -find-executable clang`" )/../include/c++/v1" && pwd )"
    project_path="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../.." && pwd )"
    ws_path="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../../.." && pwd )"
    toolchain_path="$( cd "$ws_path/elv-toolchain/dist/darwin"* && pwd )"
    clang_bin=$toolchain_path/bin/clang++
fi

$clang_bin -O0 -I../../include $stdcpp_include_flag -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit \
-c avtrailer.cpp -o avtrailer.bc
