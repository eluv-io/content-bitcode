#!/bin/bash
#
# Publish a content object
#
# Usage: QLIBID QTYPE NAME FILE  [FILE ...]
#
# Prerequisites
hash jq || exit 1

if test $# -lt 3; then
    echo "Required arguments: <QLIBID> <QTYPE> <NAME> <FILE> [<FILE> ...]"
    exit 1
fi

qlibid=$1
qtype=$2
name=$3

shift; shift; shift;
files=$*

url=http://localhost:8008
hdr="-H Accept: application/json -H Content-Type: application/json"

# Make a content object with the required parts and keys
function make_content() {

    # Create content object
    qwt=`curl -s -X POST $hdr $url/qlibs/$qlibid/q \
        -d '{"type":"'"$qtype"'","meta":{"name":"'"$name"'"}}' | jq -r .write_token`

    echo qwt: $qwt

    # Make a part for each file in the argument list
    for f in $files; do
    	fkey=`basename $f | tr " " "_"`
    	echo part: $fkey
    	qphash=`curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt/data --data-binary @$f | jq -r .part.hash`
    	curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt/meta \
                --data \{\"$fkey\":\"$qphash\"\}
    done

    # Finalize content object
    qhash=`curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt | jq -r .hash`

    echo qhash: $qhash
}

echo Library: $qlibid
echo Content type: $qtype

make_content
