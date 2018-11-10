#!/bin/bash
set -Eeuxo pipefail


url="http://q1.contentfabric.io"
qlibid="ilib1"


dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $dir

header="-H Accept: application/json -H Content-Type: application/json"
qwt=`curl -s -X POST $header $url/qlibs/$qlibid/q \
  -d \{\"meta\":\{\"bitcode\":\"avtrailer2.bc\"\}\} \
  | jq -r .write_token`
qphash=`curl -s -X POST $header $url/qlibs/$qlibid/q/$qwt/data \
  --data-binary @avtrailer.bc \
  | jq -r .part.hash`
qhash=`curl -s -X POST $header $url/qlibs/$qlibid/q/$qwt \
  | jq -r .hash`
