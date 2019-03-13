## Prerequisites
* Docker and Git installed
* These are MacOS instructions

## Steps
1. Clone https://github.com/eluv-io/content-bitcode
1. Run Docker to get a Linux build environment:
```
cd content-bitcode
docker run -ti --rm=true -v $(pwd):/content-bitcode eluvio-content-bitcode
```
3. In the Docker shell, build and publish the bitcode (**See notes below first**):
```
cd /content-bitcode/bitcode/examples/helloworld
./build.sh
./publish-bc.sh http://209.51.161.242:80 ilib2AWdn731Mrrn68nmyP8WMqUpx69M helloworld.bc
./publish-content.sh http://209.51.161.242:80 ilib2AWdn731Mrrn68nmyP8WMqUpx69M hq__QmWpLNhhiQRWjiEM9HwDUx1eXN11UZqC4Y1UeXMrVXBWE5 hellocats cats.png
```
4. Go the the URL with your browser, e.g. http://209.51.161.242/qlibs/ilib2AWdn731Mrrn68nmyP8WMqUpx69M/q/hq__QmTsVdyw1qHPjc9TnEbJpP6jVwSCy3L3NAeDh2Q71ZrXPB/rep/image

## Notes
* You should use your own IDs as arguments to the publish script:
  * qlibid: The ID of the library created by this API call: `curl -s -X POST -H "Accept: application/json" -H "Content-Type: application/json" http://209.51.161.242:80/qlibs`
  * qtype: The ID of the content type printed after publish-bc.sh runs successfully
* The fabric and the bitcode you are building must be built using the same platform and toolchain
