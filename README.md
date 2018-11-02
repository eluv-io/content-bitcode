# Bitcode Documentation:

Bitcode is the mechanism developers can use to orchestrate the fabric at a machine code level.  The bitcode produced by `clang` in its `emit-llvm` mode is directly loadable and callable by the fabric daemon.

There are 3 methods for getting setup:

1. Docker (Ubuntu 18.04 based) -- **preferred**
2. Linux (Instructions presume Ubuntu)
3. Mac

## Using Docker (preferred)

### Building the Dockerfile

After cloning this repository, there is a `Dockerfile` that is in the base of the repo.  Presuming `docker` is installed (if not, follow [these instructions](https://docs.docker.com/install/)) it is a simple matter of building the Docker image locally:

```bash
docker build -t eluvio-content-bitcode .
```

If there are changes to the `git` repo, this command will need to be re-run after a `git pull`.

### Running the Docker image

To run the Docker image after it has been built, the simplest way is to run `docker run` like so:

```bash
docker run -ti eluvio-content-bitcode
```

This simple instantiation does not mount any volumes and will keep old instances around after running.  If unfamiliar with docker, here is an example `docker` command to mount the current working directory to `/mystuff` within the container, and to remove the image instance after container exit (using the locally built image):

```bash
docker run -ti --rm=true -v $(pwd):/mystuff eluvio-content-bitcode
```

## Linux (Ubuntu 18.04)

Prerequisites:
- These steps get the correct `clang` and toolchain in place
    ```bash
    sudo apt install \
        build-essential \
        subversion\
        cmake\
        python3-dev\
        libncurses5-dev\
        libxml2-dev\
        libedit-dev\
        swig\
        doxygen\
        graphviz\
        xz-utils
    git clone https://bitbucket.org/sol_prog/clang-7-ubuntu-18.04-x86-64.git
    cd clang-7-ubuntu-18.04-x86-64
    tar xf clang_7.0.0.tar.xz
    sudo mv clang_7.0.0 /usr/local
    export PATH=/usr/local/clang_7.0.0/bin:$PATH
    export LD_LIBRARY_PATH=/usr/local/clang_7.0.0/lib:$LD_LIBRARY_PATH
    ```

## Mac

[Xcode](https://developer.apple.com/xcode/) must be installed and `clang` available at the command line

```bash
mymac> which clang
/usr/bin/clang
```

# Working with the bitcode

##  Building:
  - `content-bitcode/scripts/build_all.sh`
    - Builds all bitcode modules except helloworld
  - `content-bitcode/bitcode/helloworld/build.sh`
    - Builds `helloworld.bc`

##  Deploying Content:
  - Currently there is only one script to deploy called `populate-dev.sh` in the helloworld folder.  This script will drive the fabric using a combination of bitcode and the fabrics http handlers.  This script will:
    - create a new content library
    - publish helloworld bitcode type into it
    - create part indexed by "image"
    - deliver a full URL to the base of the rep handler

## Running
Once deployed to a fabric node the bitcode is callable using curl or a browser. The helloworld sample responds to three handler calls
  - `http://base_url/libid/q/phash/rep/image`
    - returns an image as binary
  - `http://base_url/libid/q/phash/rep/helloworld`
    - return html that references rep/image
  - `http://base_url/libid/q/phash/call/helloworld`
    - returns text with info from fabric
