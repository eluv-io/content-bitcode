#!/bin/bash

CLANGPP=clang++

if test "$GOPATH" == ""; then
    echo "GOPATH is not set"
    exit 1
fi

if test "$CGO_CPPFLAGS" == ""; then
    echo "CGO_CPPFLAGS is not set. Set using init-cgopath.sh"
    exit 1
fi


if test $# -eq 1; then
    EXTRA_FLAGS="$1"
fi

cd "$GOPATH"/bitcode

(
cd test

$CLANGPP -O0 -g $EXTRA_FLAGS -I../include $CGO_CPPFLAGS -Wall -emit-llvm \
	 -std=c++14 -fno-use-cxa-atexit \
	 -c test_callctx.cpp -o test_callctx.bc

$CLANGPP -O0 -g $EXTRA_FLAGS -I../include $CGO_CPPFLAGS -Wall -emit-llvm \
	 -std=c++14 -fno-use-cxa-atexit \
	 -c test_llvm.cpp -o test_llvm.bc

$CLANGPP -O0 -g $EXTRA_FLAGS -I../include $CGO_CPPFLAGS -Wall -emit-llvm \
	 -std=c++14 -fno-use-cxa-atexit \
	 -c test_jpc.cpp -o test_jpc.bc
)

