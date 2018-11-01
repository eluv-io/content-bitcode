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
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/el_constants.h"

using nlohmann::json;


/*
clang++ -std=c++14 -stdlib=libstdc++ -I ../include -I /home/jan/SSD2/elv-debug/dist/linux-glibc.2.27/include 
-g -c -emit-llvm -O0 tagger.cpp -fno-use-cxa-atexit -o tagger.bc
*/

/*
 * Outputs the data from key "image"
 */
int
taggit(int outsz, char *outbuf, char* qlibid, char* qhash)
{
    // *************************************
    // THIS IS DUP'd FROM AVMASTER2000.CPP 
    // JUST A PLACEHOLDER
    // *************************************

    return -1;
}

/*
 * Dispatch content requests
 *
 * Arguments:
 *   qlibid       - content library ID
 *   qhash        - content 'hash'
 *   URL path (e.g.: "/html")
 *
 * Example URLs:
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/video
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/html
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/image
 */
extern "C" int
tagger(int outsz, char *outbuf, const char *argbuf)
{
    int res = -1;
    ArgumentBuffer argBuf(argbuf); 
    int argc = argBuf.Count();

    // argc should be 3
    if (argc < 3) return -1;
    printf("argc=%d lib=%s url=%s\n", argc, argBuf[0], argBuf[2]);

    char *qlibid = argBuf[0];
    char *qhash = argBuf[1];
    // char* pMetaRequest = argBuf[2];

    // int szUrl = 4*1024;
    // char url[szUrl];
    // /*FIXME: Need a way to get the url instead of localhost:8008 */
    // snprintf(url, szUrl, "http://localhost:8008/qlibs/%s/q/%s/meta/%s",qlibid,qhash,pMetaRequest);
    // if (strcmp(pMetaRequest, "/tag") == 0){  // really need to return error if not matching any
    //     return taggit(outsz, outbuf, qlibid, qhash);
    // }
    return taggit(outsz, outbuf, ;
}

