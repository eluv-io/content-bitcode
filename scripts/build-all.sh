#!/bin/bash
set -Eeuo pipefail

# TODO
#   - Add script argument to select DEBUG or RELEASE, i.e. -g or not
#   - Move compilation command into a function

clang_bin=clang++

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

shared_flags="-I../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit"
qspecdir="$dir/../bitcode"
cd $qspecdir

(
cd avmaster
$clang_bin -O0 -g $shared_flags \
	 -c avmaster2000.cpp -o avmaster2000.imf.bc

$clang_bin -O0 -g $shared_flags \
	 -c adsmanager.cpp -o adsmanager.bc
)

(
cd avlive
$clang_bin $shared_flags \
    -c live_rtp.cpp -o avlive.rtp.bc
)

(
cd submission
$clang_bin -O0 $shared_flags \
    -c submission.cpp -o submission.bc
)

(
cd library
$clang_bin -O0 $shared_flags \
    -c library.cpp -o library.bc
)


(
cd video
$clang_bin $shared_flags \
    -c video.cpp -o video.bc
)


(
cd search
$clang_bin $shared_flags \
    -c search.cpp -o search.bc
)

(
cd proxy
$clang_bin $shared_flags \
   -c proxy.cpp -o proxy.bc
)

(
cd challenge
$clang_bin $shared_flags \
    -c challenge.cpp -o challenge.bc
)
