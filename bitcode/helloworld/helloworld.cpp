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

elv_return_type make_html(BitCodeCallContext* ctx, const char *url)
{
  /*TODO: this embedded html should be a template resource, part of the content */
  const std::string body_format(R"(
    <!doctype html>
    <html>
        <head>
            <title>Eluvio Fabric HelloWorld</title>
            <style>
                body {
                    background-color:black;
                }
            </style>
        </head>
      <body>
        <div>
            HelloWorld <img src="%s">
        </div>
      </body>
      </html>)");

    const char* headers = "text/html";

    std::string str_body = string_format(body_format, url);
    LOG_INFO(ctx, "make_html", "BODY", str_body.c_str());
    ctx->Callback(200, headers, str_body.length());
    std::vector<unsigned char> htmlData(str_body.c_str(), str_body.c_str() + str_body.length());
    auto ret = ctx->WriteOutput(htmlData);

    return ctx->make_success();
}
/*
 * Outputs the data from key "image"
 */
elv_return_type make_image(BitCodeCallContext* ctx)
{
    char *headers = (char *)"image/png";

    auto phash = ctx->SQMDGetString((char *)"image");
    if (phash == "") {
        const char* msg = "Failed to read key";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_image thumbnail", "part_hash", phash);

    /* Read the part in memory */
    uint32_t psz = 0;
    auto body = ctx->QReadPart(phash.c_str(), 0, -1, &psz);
    if (body->size() == 0) {
        const char* msg = "QReadPart Failed to read resource part";
        LOG_ERROR(ctx, msg, "HASH", phash);
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_image thumbnail",  "part_size", (int)psz);

    ctx->Callback(200, headers, psz);
    return ctx->WriteOutput(*(body.get()));
}

elv_return_type content(BitCodeCallContext* ctx,  JPCParams& p)
{
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }

    char* pContentRequest = (char*)(path.first.c_str());

    LOG_INFO(ctx, "content", pContentRequest);

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/image") == 0) {
        LOG_INFO(ctx, "making REP /image");
        return make_image(ctx);
    }

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/helloworld") == 0) {
        LOG_INFO(ctx, "making REP /helloWorld");
        return make_html(ctx, "image");
    }
    /* Check for DASH extensions */

    const char* msg = "unknown type requested";
    return ctx->make_error(msg, E(msg).Kind(E::Other));

}

elv_return_type helloworld(BitCodeCallContext* ctx,  JPCParams& p)
{
    auto phash = ctx->SQMDGetString((char *)"name");
    if (phash == ""){
        const char* msg = "failed to get MD key name";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }

    const char* headers = "text/html";

    std::string body_format = R"(Hello my name is %s)";
    std::string str_body = string_format(body_format, phash.c_str());
    ctx->Callback(200, headers, str_body.length());
    std::vector<unsigned char> htmlData(str_body.c_str(), str_body.c_str() + str_body.length());
    auto ret = ctx->WriteOutput(htmlData);

    return ctx->make_success();
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(helloworld)
END_MODULE_MAP()
