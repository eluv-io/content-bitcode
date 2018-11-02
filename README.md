# Bitcode Documentation:

Bitcode is the mechanism developers can use to orchestrate the fabric at a machine code level.  The bitcode produced by clang in its emit-llvm mode is directly loadable and callable by the fabric daemon.

## Installation:

Prerequisites:
- linux
  - sudo apt install build-essential subversion cmake python3-dev libncurses5-dev libxml2-dev libedit-dev swig doxygen graphviz xz-utils
  - git clone https://bitbucket.org/sol_prog/clang-7-ubuntu-18.04-x86-64.git
  - cd clang-7-ubuntu-18.04-x86-64
  - tar xf clang_7.0.0.tar.xz
  - sudo mv clang_7.0.0 /usr/local
  - export PATH=/usr/local/clang_7.0.0/bin:$PATH
  - export LD_LIBRARY_PATH=/usr/local/clang_7.0.0/lib:$LD_LIBRARY_PATH

- mac : xcode must be installed and clang available at the command line
  - e.g. mymac>which clang --> /usr/bin/clang

##  Building:
  - content-bitcode/scripts/build_all.sh
    - Builds all bitcode modules except helloworld
  - content-bitcode/bitcode/helloworld/build.sh
    - Builds helloworld.bc

##  Deploying Content:
  - Currently there is only one script to deploy called populate-dev.sh in the helloworld folder.  This script will drive the fabric using a combination of bitcode and the fabrics http handlers.  This script will:     - create a new content library
    - publish helloworld bitcode type into it
    - create part indexed by "image"
    - deliver a full URL to the base of the rep handler

## Running
Once deployed to a fabric node the bitcode is callable using curl or a browser. The helloworld sample responds to three handler calls
  - http://base_url/libid/q/phash/rep/image
    - returns an image as binary
  - http://base_url/libid/q/phash/rep/helloworld
    - return html that references rep/image
  - http://base_url/libid/q/phash/call/helloworld
    - returns text with info from fabric

