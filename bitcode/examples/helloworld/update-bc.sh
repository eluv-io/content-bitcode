#!/bin/bash

#
# Update a content object to a new version of the bitcode
#
# Usage: <qlibid> <qid> <bitcode name>
#

# Prerequisites
hash jq || exit 1

if test $# -lt 3; then
    echo "Required arguments: <QLIBID> <QID> <QTYPE-NAME>"
    exit 1
fi

url=http://localhost:8008
hdr="-H Accept: application/json -H Content-Type: application/json"

qlibid=$1
qid=$2
qtypename=$3

#
# Update bitcode for content object
#

# Extract the hash of the content type
qtype=`curl -s $hdr $url/naming/$qtypename | jq -r .target`

echo qid=$qid
echo qtype=$qtype

# Create a new version of the content object
qwt=`curl -s -X POST $hdr $url/qlibs/$qlibid/qid/$qid \
        -d '{"type":"'"$qtype"'","meta":{}}' | jq -r .write_token`

echo qwt=$qwt

# Finalize content object
qhash=`curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt | jq -r .hash`

echo qhash=$qhash

echo "New content:"
echo "$url/qlibs/$qlibid/q/$qid"
