#!/bin/bash
set -Eeuxo pipefail

source /p/e/git/content-fabric/init-gopath.sh
source /p/e/git/content-fabric/init-cgoenv.sh

/p/e/git/content-fabric/bin/qfab_cli submit video \
--log-level debug \
--url http://q1.contentfabric.io \
--library ilib1 \
--name "BBB Trailer" \
--description "No time to waste" \
--source /p/e/demo/media/SampleVideo_720x480_30mb.mp4 \
--image /p/e/demo/media/THUMBNAILS/bbb.png  \
--type hq__QmfJ84sRiDYo2HBm8eoVGHT4c7CHiDi82vGbH2XDT3Lc89 \
--scriptsdir /p/e/git/MP/ondemand \
--caddr "0x17a35784f29dbee8084864d609e865333f5dd121"
