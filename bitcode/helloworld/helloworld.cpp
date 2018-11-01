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

std::pair<nlohmann::json, int> make_html(BitCodeCallContext* ctx, const char *url)
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
    printf("BODY = %s\n", str_body.c_str());
    const char* response_template = R"({"http" : {"status" : %d, "headers" : {"Content-Type" : ["%s"], "Content-Length" :  ["%d"]} }})";

    auto response = string_format(std::string(response_template), 200, headers, str_body.length());
    printf("RESPONSE = %s\n", response.c_str());
    nlohmann::json j_response = json::parse(response);
    ctx->Callback(j_response);
    std::vector<unsigned char> htmlData(str_body.c_str(), str_body.c_str() + str_body.length());
    auto ret = ctx->WriteOutput(htmlData);

    return ctx->make_success();
}
/*
 * Outputs the data from key "image"
 */
std::pair<nlohmann::json, int> make_image(BitCodeCallContext* ctx)
{
    char *headers = (char *)"image/png";

    auto phash = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetString((char *)"image"));
    if (phash.get() == NULL) {
        LOG_ERROR(ctx, "Failed to read key");
        return ctx->make_error("Failed to read key", -1);
    }
    LOG_INFO(ctx, "DBG-AVMASTER thumbnail part_hash=", phash.get());

    /* Read the part in memory */
    uint32_t psz = 0;
    auto body = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(phash.get(), 0, -1, &psz));
    if (body.get() == NULL) {
        LOG_ERROR(ctx, "Failed to read resource part");
        return ctx->make_error("Failed to read resource part", -2);
    }
    LOG_INFO(ctx, "DBG-AVMASTER thumbnail part_size=", (int)psz);

    std::string response_template(R"({"http" : {"status" : %d, "headers" : {"content-type" : ["%s"], "content-length" :  ["%d"]} }})");
    auto response = string_format(response_template, 200, headers, psz);
    nlohmann::json j_response = json::parse(response);
    ctx->Callback(j_response);
    std::vector<unsigned char> out(body.get(), body.get()+psz);
    auto ret = ctx->WriteOutput(out);

    return ctx->make_success();
}

std::pair<nlohmann::json,int> content(BitCodeCallContext* ctx,  JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }

    char* pContentRequest = (char*)(params._path.c_str());

    LOG_INFO(ctx, "content=%s", pContentRequest);

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/image") == 0) {
        LOG_INFO(ctx, "REP /image");
        return make_image(ctx);
    }

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/helloworld") == 0) {
        LOG_INFO(ctx, "REP /helloWorld");
        return make_html(ctx, "image");
    }
    /* Check for DASH extensions */

    return ctx->make_error("unknown type requested", -17);

}

std::pair<nlohmann::json,int> helloworld(BitCodeCallContext* ctx,  JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    auto phash = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetString((char *)"name"));

    const char* headers = "text/html";

    std::string body_format = R"(Hello my name is %s)";
    std::string str_body = string_format(body_format, phash.get());
    const char* response_template = R"({"http" : {"status" : %d, "headers" : {"Content-Type" : ["%s"], "Content-Length" :  ["%d"]} }})";

    auto response = string_format(std::string(response_template), 200, headers, str_body.length());
    printf("RESPONSE = %s\n", response.c_str());
    nlohmann::json j_response = json::parse(response);
    ctx->Callback(j_response);
    std::vector<unsigned char> htmlData(str_body.c_str(), str_body.c_str() + str_body.length());
    auto ret = ctx->WriteOutput(htmlData);

    return ctx->make_success();
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(helloworld)
END_MODULE_MAP()
