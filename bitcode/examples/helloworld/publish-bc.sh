#!/bin/bash
set -Eeuo pipefail

#
# Publish bitcode to fabric
#
# Usage: fabric_url qlibid bc_filepath
# Example: ./publish-bc.sh http://209.51.161.242:80 ilib2AWdn731Mrrn68nmyP8WMqUpx69M helloworld.bc
#

# Prerequisites
hash jq || exit 1

if test $# -lt 3; then
    echo "Required arguments: fabric_url qlibid bc_filepath"
    exit 1
fi

url=$1
qlibid=$2
bcpath=$3
header="-H Accept: application/json -H Content-Type: application/json"

# Create bitcode content types
bcfilename=`basename $bcpath`
echo "Publish: $bcfilename"

if ! test -e "$bcpath"; then
    echo "File not found: $bcpath"
    exit 1
fi
qwt=`curl -s -X POST $header $url/qlibs/$qlibid/q \
    -d \{\"meta\":\{\"bitcode\":\"$bcfilename\"\}\} | jq -r .write_token`
qphash=`curl -s -X POST $header $url/qlibs/$qlibid/q/$qwt/data --data-binary @$bcpath | jq -r .part.hash`
qhash=`curl -s -X POST $header $url/qlibs/$qlibid/q/$qwt | jq -r .hash`

# Register name
bcname="${bcfilename%.*}"
curl -s -X PUT -H "Content-Type: application/json" \
     $url/naming -d \{\"name\":\"$bcname\",\"target\":\"$qhash\"\} | jq -r .
echo "$qhash"
