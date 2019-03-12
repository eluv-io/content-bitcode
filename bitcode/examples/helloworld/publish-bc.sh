#!/bin/bash

#
# Publish bitcode to fabric
#
# Usage: <qlibid> <path-to-bc-file>
#
# If qlibid is empty - create a new library
#

# Prerequisites
hash jq || exit 1

if test $# -lt 2; then
    echo "Required arguments: <QLIBID> <FILE.BC>"
    exit 1
fi

qlibid=$1
BCPATH=$2

URL=http://localhost:8008
HDR="-H Accept: application/json -H Content-Type: application/json"

# Create bitcode content types
BC=`basename $BCPATH`
echo "Publish: $BC"

if ! test -e "$BCPATH"; then
    echo "File not found: $BCPATH"
    exit 1
fi
qwt=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q \
    -d \{\"meta\":\{\"bitcode\":\"$BC\"\}\} | jq -r .write_token`
qphash=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q/$qwt/data --data-binary @$BCPATH | jq -r .part.hash`
qhash=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q/$qwt | jq -r .hash`

# Register name
BCNAME="${BC%.*}"
curl -s -X PUT -H "Content-Type: application/json" \
     $URL/naming -d \{\"name\":\"$BCNAME\",\"target\":\"$qhash\"\} | jq -r .
echo "$qhash"
