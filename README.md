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



