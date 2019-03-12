#!/bin/bash
set -Eeuxo pipefail

source /p/e/git/ws/content-fabric/init-gopath.sh
source /p/e/git/ws/content-fabric/init-cgoenv.sh

# Doesn't work anymore
/p/e/git/ws/content-fabric/bin/qfab_cli submit video \
--log-level debug \
--url http://localhost:8008 \
--library ilibmXGCuPZKWSKac13T3dAguedhEbQ \
--name "BBB Trailer" \
--description "BIG BUCK BUNNY!!!" \
--source /p/e/demo/media/SampleVideo_720x480_30mb.mp4 \
--image /p/e/demo/media/THUMBNAILS/bbb.png  \
--type hq__QmVYxxWXccDbvT6J9xLKHC6JPHCU8eZFBHjePwxXiCLDft \
--scriptsdir /p/e/git/MP/ondemand \
--caddr "0x17a35784f29dbee8084864d609e865333f5dd121"
