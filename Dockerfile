FROM ubuntu:18.04

RUN apt-get update && \
    apt-get install -y \
        git \
        build-essential \
        subversion \
        cmake \
        python3-dev \
        libncurses5-dev \
        libxml2-dev \
        libedit-dev \
        swig \
        doxygen \
        graphviz \
        xz-utils \
        jq \
        curl \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /scratch
RUN git clone https://bitbucket.org/sol_prog/clang-7-ubuntu-18.04-x86-64.git && \
    cd clang-7-ubuntu-18.04-x86-64 && \
    tar -C /usr/local -xf clang_7.0.0.tar.xz && \
    cd .. && \
    rm -rf clang-7-ubuntu-18.04-x86-64

ENV PATH=/usr/local/clang_7.0.0/bin:${PATH}
ENV LD_LIBRARY_PATH=/usr/local/clang_7.0.0/lib:${LD_LIBRARY_PATH}

WORKDIR /src/content-bitcode
COPY LICENSE /src/content-bitcode/
COPY bitcode/ /src/content-bitcode/bitcode/
COPY scripts/ /src/content-bitcode/scripts/

RUN scripts/build-all.sh

ENTRYPOINT /bin/bash
