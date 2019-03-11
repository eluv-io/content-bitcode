#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "argutils.h"
#include "fixup-cpp.h"
#include "utils.h"
#include "el_cgo_interface.h"
#include "el_constants.h"

using nlohmann::json;


/*
clang++ -std=c++14 -stdlib=libstdc++ -I ../include -I /home/jan/SSD2/elv-debug/dist/linux-glibc.2.27/include 
-g -c -emit-llvm -O0 campaign.cpp -fno-use-cxa-atexit -o campaign.bc
*/


/*
    find_ads(int outsz, char *outbuf, char* qlibid, char* qhash, std::vector<std::string>& tags)

    outsz - output buffer size [in/out]
    outbuf - free form byte output [out] json text of ranked tags in json array
    qlibid - stringized libid 
    qhash  - stringized meta hash
    tags   - vector of strings with tags of interest
*/
int
find_ads(int outsz, char *outbuf, char* qlibid, char* qhash, std::vector<std::string>& tags){
    // *************************************
    // THIS IS DUP'd FROM AVMASTER2000.CPP 
    // JUST A PLACEHOLDER
    // *************************************
    return -1;
}



