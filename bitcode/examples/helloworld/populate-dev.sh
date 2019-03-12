#!/bin/bash

#
# Populate fabric 'dev'
# Build new qlib
# Loads helloworld.bc into new lib creating helloworld type
# Creates new content object in lib
# Load command line parameter(s) as a new part in new content object
#   - This is used by the make_image function in helloworld.cpp
# Turns out base URL to rep

# Prerequisites
hash jq || exit 1

if [ "$#" -ne 1 ]; then
    echo "A single parameter pointing to a valid image file is required!!"
    exit -1
fi

bclist="helloworld.bc"
bctype="helloworld"
pubname="image"
metaname=$1

URL=http://localhost:8008
HDR="-H Accept: application/json -H Content-Type: application/json"

# Create a library
qlibid=`curl -s -X POST $HDR $URL/qlibs | jq -r .id`
echo "Library: $qlibid"

# Publish the bc listed in 'bclist'
./publish-bc.sh $qlibid $bclist
# Create bitcode content types
BC=`basename $bclist`
echo "Publish: $BC"

if ! test -e "$bclist"; then
    echo "File not found: $bclist"
    exit 1
fi
qwt=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q \
    -d \{\"meta\":\{\"bitcode\":\"$BC\"\}\} | jq -r .write_token`
qphash=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q/$qwt/data --data-binary @$bclist | jq -r .part.hash`
qhash=`curl -s -X POST $HDR $URL/qlibs/$qlibid/q/$qwt | jq -r .hash`

# Register name
BCNAME="${BC%.*}"
curl -s -X PUT -H "Content-Type: application/json" \
     $URL/naming -d \{\"name\":\"$BCNAME\",\"target\":\"$qhash\"\} | jq -r .

url=http://localhost:8008
hdr="-H Accept: application/json -H Content-Type: application/json"

# Make a content object with the required parts and keys
function make_content() {

    # Create content object
    qwt=`curl -s -X POST $hdr $url/qlibs/$qlibid/q -d '{"type":"'"$qhash"'","meta":{"name":"'"$pubname"'"}}' | jq -r .write_token`

    echo qwt: $qwt

    # Make a part for each file in the argument list
    for f in $metaname; do
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
}

echo Library: $qlibid
echo Content type: $qhash

make_content

echo URL: $url/qlibs/$qlibid/q/$qhash/rep/

#./publish-content.sh $qlibid $qhash $pubname $metaname

