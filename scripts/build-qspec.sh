#!/bin/bash
set -Eeuo pipefail

# TODO
#   - Add script argument to select DEBUG or RELEASE, i.e. -g or not
#   - Move compilation command into a function

clang_bin=clang++

if test "$GOPATH" == ""; then
    echo "GOPATH is not set"
    exit 1
fi

if test "$CGO_CPPFLAGS" == ""; then
    echo "CGO_CPPFLAGS is not set. Set using init-cgoenv.sh"
    exit 1
fi

extra_flags=""
if test $# -eq 1; then
    extra_flags="$1"
fi

shared_flags="-I../include -Wall -emit-llvm -std=c++14 -fno-use-cxa-atexit"

cd "$GOPATH"/bitcode

(
cd avmaster
$clang_bin -O0 -g $extra_flags $CGO_CPPFLAGS $shared_flags \
	 -c avmaster2000.cpp -o avmaster2000.imf.bc

$clang_bin -O0 -g $extra_flags $CGO_CPPFLAGS $shared_flags \
	 -c adsmanager.cpp -o adsmanager.bc
)

(
cd avlive
$clang_bin -O0 $extra_flags $CGO_CPPFLAGS $shared_flags \
    -c live_rtp.cpp -o avlive.rtp.bc
)

(
cd submission
$clang_bin -O0 $extra_flags $CGO_CPPFLAGS $shared_flags \
    -c submission.cpp -o submission.bc
)

(
cd video
$clang_bin -O0 $extra_flags $CGO_CPPFLAGS $shared_flags \
    -c video.cpp -o video.bc
)

(
cd library
$clang_bin -O0 $extra_flags $CGO_CPPFLAGS $shared_flags \
    -c library.cpp -o library.bc
)

