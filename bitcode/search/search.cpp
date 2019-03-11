/*
  clang++ -Wall -std=c++14 -I pointer to /nlohman/json/include -emit-llvm -fno-use-cxa-atexit -c -g helloWorld.cpp -o helloWorld.bc
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "nlohmann/json.hpp"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
using namespace elv_context;

using nlohmann::json;

elv_return_type search(BitCodeCallContext *ctx, JPCParams &p)
{
    // get the search parameters : .../call/search?keywords=...&topn=16
    auto qparams = ctx->QueryParams(p);
    if (qparams.second.IsError())
    {
        return ctx->make_error("could not read query parameters", qparams.second);
    }

    // check query parameters
    if (qparams.first.find("keywords") == qparams.first.end())
    {
        return ctx->make_error("the parameter \"keywords\" could not be found", E::BadHttpParams);
    }

    if (qparams.first.find("topn") == qparams.first.end())
    {
        return ctx->make_error("the parameter \"topn\" could not be found", E::BadHttpParams);
    }

    auto jparams = nlohmann::json(qparams.first);

    // call the fabric search module
    auto funcname = std::string("Search");
    auto result = ctx->Call(funcname, jparams, ctx->goext);
    if (result.second.IsError())
    {
        return ctx->make_error("the search failed", result.second);
    }

    // sends back the result of the search
    std::string str = result.first.get<std::string>();
    std::vector<unsigned char> data(str.begin(), str.end());
    ctx->WriteOutput(data);
    return ctx->make_success();
}

BEGIN_MODULE_MAP()
MODULE_MAP_ENTRY(search)
END_MODULE_MAP()
