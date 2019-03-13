#!/bin/bash
set -Eeuo pipefail

#
# Publish a content object
#
# Usage: fabric_url qlibid qtype publish_name file [file ...]
# Example:
#   ./publish-content.sh \
#   http://209.51.161.242:80 \
#   ilib2AWdn731Mrrn68nmyP8WMqUpx69M \
#   hq__QmWpLNhhiQRWjiEM9HwDUx1eXN11UZqC4Y1UeXMrVXBWE5 \
#   hellocats \
#   cats.png
#

hash jq || exit 1

if test $# -lt 4; then
    echo "Required arguments: fabric_url qlibid qtype publish_name file [file ...]"
    exit 1
fi

url=$1
qlibid=$2
qtype=$3
name=$4
echo "lib=$qlibid type=$qtype name=$name"

shift; shift; shift;
files=$*
echo "files=$files"

hdr="-H Accept: application/json -H Content-Type: application/json"

# Make a content object with the required parts and keys
function make_content() {

    # Create content object
    qwt=`curl -s -X POST $hdr $url/qlibs/$qlibid/q -d '{"type":"'"$qtype"'","meta":{"name":"'"$name"'"}}' | jq -r .write_token`

    echo qwt: $qwt

    # Make a part for each file in the argument list
    for f in $files; do
    	fkey=`basename $f | tr " " "_"`
    	echo part: $fkey
    	qphash=`curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt/data --data-binary @$f | jq -r .part.hash`
    	curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt/meta \
                --data \{\"$fkey\":\"$qphash\"\}
    	curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt/meta \
                --data \{\"image\":\"$qphash\"\}
    done

    # Finalize content object
    qhash=`curl -s -X POST $hdr $url/qlibs/$qlibid/q/$qwt | jq -r .hash`

    echo qhash: $qhash
    echo URL: $url/qlibs/$qlibid/q/$qhash/rep/image
}

echo Library: $qlibid
echo Content type: $qtype

make_content
