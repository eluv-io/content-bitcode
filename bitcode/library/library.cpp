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
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/cddl.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"

using namespace elv_context;

/*
 *  Outputs video from key "video"
 */
std::pair<nlohmann::json, int> make_video(BitCodeCallContext* ctx){
    auto phash = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)"video"));

    if (phash.get() == NULL) {
        printf("Failed to read resource part\n");
        return ctx->make_error("Failed to read resource part", -101);
    }
    printf("DBG-VIDEO video part_hash=%s\n", phash.get());
    /* Read the part in memory */
    uint32_t psz = 0;
    uint64_t max_size = 39 * 1024 * 1024; // FIXME - need ability to stream result
    auto body = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(phash.get(), 0, max_size, &psz));
    if (body.get() == NULL) {
        printf("Failed to read resource part\n");
        return ctx->make_error("Failed to read resource part", -102);
    }
    printf("DBG-SUBMISSION webisod.video part_size=%d\n", (int)psz);

    std::vector<uint8_t> vec(body.get(), body.get() + strlen(body.get()));
    auto res = ctx->WriteOutput(vec);
    if (res.second == 0){
        return ctx->make_success();
    }else{
        return ctx->make_error("write output failed", -103);
    }
}

/*
 * Outputs the data from key "image"
 */
std::pair<nlohmann::json, int> make_image(BitCodeCallContext* ctx){
    char *headers = (char *)"image/png";

    LOG_INFO(ctx, "DBG-LIBRARY thumbnail from eluv.image");
    auto phash = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetString((char*)"eluv.image"));
    if (phash.get() == NULL) {
        printf("Failed to read key\n");
        return ctx->make_error("Failed to read key", -144);
    }
    LOG_INFO(ctx, "DBG-LIBRARY thumbnail part_hash=", phash.get());

    /* Read the part in memory */
    uint32_t psz = 0;
    auto body = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(phash.get(), 0, -1, &psz));
    if (body.get() == NULL) {
        LOG_ERROR(ctx, "Failed to read resource part");
        return ctx->make_error("Failed to read resource part", -2);
    }
    LOG_INFO(ctx, "DBG-LIBRARY thumbnail part_size=", (int)psz);

    std::string response_template(R"({"http" : {"status" : %d, "headers" : {"content-type" : ["%s"], "content-length" :  ["%d"]} }})");
    auto response = string_format(response_template, 200, headers, psz);
    nlohmann::json j_response = json::parse(response);
    ctx->Callback(j_response);
    std::vector<unsigned char> out(body.get(), body.get()+psz);
    auto ret = ctx->WriteOutput(out);

    return ctx->make_success();
}

/*
  Renders an html page with video from /rep/video
*/
std::pair<nlohmann::json, int> make_html(BitCodeCallContext* ctx, char *url){
  size_t szUrl = strlen(url) + 1;
  char videoUrl[szUrl+5];
  strcpy(videoUrl,url);

  char *res = strrchr(videoUrl, '/');
  if(strcmp(res,"/html") != 0)
    return ctx->make_error("bad format to url missing .html", -118);

  /* replace html with video */
  if (res != NULL)
      strcpy(res, "/video");

    /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = (char*)"<!doctype html>"        \
        "<html>"                                \
            "<head>" \
                "<title>Eluvio Fabric Player</title>" \
                "<style>" \
                    "video {" \
                      "display: block; margin-left: auto; margin-right: auto; height: 100vh; width: auto;" \
                    "}" \
                    "body {" \
                      "background-color:black;"
                    "}"
                "</style>" \
            "</head>" \
            "<body>" \
                "<div>" \
                    "<video autoplay controls>" \
                    "<source src=\"%s\">"
                    "</video>" \
                "</div>" \
            "</body>" \
        "</html>";

    /* Build html page with video source */
    int szBody = strlen(bodyFormat) + strlen(videoUrl) + 1;
    char body[szBody];
    snprintf(body,szBody,bodyFormat,videoUrl);

    /* Prepare output */
    std::vector<uint8_t> vec(body, body + strlen(body));
    auto write_res = ctx->WriteOutput(vec);
    if (write_res.second == 0){
        return ctx->make_success();
    }else{
        return ctx->make_error("write output failed", -119);
    }
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
std::pair<nlohmann::json, int> content(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    char* pContentRequest = (char*)(params._path.c_str());


    if (strcmp(pContentRequest, "/video") == 0)  // really need to return error if not matching any
      return make_video(ctx);
    //else if (strcmp(pContentRequest, "/html") == 0)
    //  return make_html(ctx, url);
    else if (strcmp(pContentRequest, "/image") == 0)
      return make_image(ctx);
    else
        return ctx->make_error("unknown  service requested must be one of /hmtl /video or /image", -213);
}

int cddl_num_mandatories = 4;
char *cddl = (char*)"{"
    "\"title\" : bytes,"
    "\"description\" : text,"
    "\"video\" : eluv.video,"
    "\"image\" : eluv.img"
    "}";

/*
 * Validate content components.
 *
 * Returns:
 *  -1 in case of unexpected failure
 *   0 if valid
 *  >0 the number of validation problems (i.e. components missing or wrong)
 */
std::pair<nlohmann::json,int> validate(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    int found = cddl_parse_and_check(ctx, cddl);

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/cddl_num_mandatories);

    std::vector<uint8_t> vec(valid_pct, valid_pct + 4);
    auto res = ctx->WriteOutput(vec);
    if (res.second == 0){
        return ctx->make_success();
    }else{
        return ctx->make_error("write output failed", -103);
    }
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(validate)
END_MODULE_MAP()


#ifdef UNIT_TEST

char *QReadPart(char *a1, char *a2, char *a3,
    uint32_t o, uint32_t s, uint32_t *a4)  { return "NOT IMPLEMENTED"; }
char *KVGet(char *a1, char *a2, char *a3)                    { return "NOT IMPLEMENTED"; }
char *KVList(char *a1, char *a2)                             { return "NOT IMPLEMENTED"; }

int main(int argc, char *argv[])
{
    printf("CDDL:\n%s\n", cddl);
    cddl_parse_and_check(cddl, "", "");
    return 0;
}

#endif
