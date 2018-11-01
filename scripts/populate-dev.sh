#!/bin/bash

#
# Populate fabric 'dev' environment
#

# Prerequisites
hash jq || exit 1

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
qspecdir="$dir/../bitcode"

bclist="$qspecdir/avmaster/avmaster2000.imf.bc \
        $qspecdir/avmaster/adsmanager.bc \
        $qspecdir/avlive/avlive.rtp.bc \
        $qspecdir/submission/submission.bc \
        $qspecdir/video/video.bc \
        $qspecdir/library/library.bc"

URL=http://localhost:8008
HDR="-H Accept: application/json -H Content-Type: application/json"

# Create a library
qlibid=`curl -s -X POST $HDR $URL/qlibs | jq -r .id`
echo "Library: $qlibid"

# Publish the bc listed in 'bclist'
for bcpath in $bclist; do
    $dir/publish-bc.sh $qlibid $bcpath
done

# Create ads Library
adslibid=`curl -s -X POST $HDR $URL/qlibs | jq -r .id`
echo "Created Ads Library: $adslibid"

hash=`curl -s http://localhost:8008/naming/adsmanager | jq -r .target`

echo "Publishing adsmanager object."
$dir/publish-content.sh $adslibid $hash "ADS MANAGER"

meta="{\"name\":\"Ads\",\"description\":\"\"}"
# Register name
curl -s -X PUT -H "Content-Type: application/json" \
     $URL/naming -d '{"name":"'$adslibid'","target":"{\"name\":\"Ads\"}"}' | jq -r .
