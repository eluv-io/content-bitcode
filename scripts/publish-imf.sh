#!/bin/bash -e

if test "$GOPATH" == ""; then
    echo "GOPATH is not set"
    exit 1
fi

pushd "$GOPATH/src/eluvio"

echo "publishing imf"

qlibID=$1
if [ -z "$qlibID" ]; then
  res=$(../../bin/qapi_cli library create)
  qlibID=$(echo "$res" | egrep -o "ilib\w*")
fi
echo "qlibID=\"$qlibID\""

out=$(../../bin/qapi_sample publish $qlibID "" /Users/luk/dev/qluvio/sinatra_dash/imf | grep "CONTENT FINAL")
echo "$out"
qID=$(echo "$out" | egrep -o "iq__\w*")
echo "qID=\"$qID\""
qHash=$(echo "$out" | egrep -o "hq__\w*")
echo "qHash=\"$qHash\""

#qID=$(../../bin/qapi_cli library show $qlibID | egrep -o "iq__\w*")
#echo "qID=\"$qID\""
#
#qHash=$(../../bin/qapi_cli content show --library $qlibID $qID | egrep -o -m 1 "hq__\w*")
#echo "qHash=\"$qHash\""

echo "running qfab daemon..."

../../bin/qfab daemon > /dev/null &
pid=$?

sleep 1

echo "retrieving manifest..."
curl -i -u test:test http://localhost:8008/qlibs/$qlibID/q/$qHash/rep/dash/EN.mpd
echo

echo
echo "retrieving init segment..."
curl -is -u test:test http://localhost:8008/qlibs/$qlibID/q/$qHash/rep/dash/EN-1280x544-1300000-init.m4v > /tmp/res; head -c 900 /tmp/res

echo
echo "retrieving segment 1..."
curl -is -u test:test http://localhost:8008/qlibs/$qlibID/q/$qHash/rep/dash/EN-1280x544-1300000-1.m4v > /tmp/res; head -c 900 /tmp/res

echo
echo "stopping daemon..."
kill -TERM $pid

popd
