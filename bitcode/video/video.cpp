
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
elv_return_type make_video(BitCodeCallContext* ctx){
    auto phash = ctx->KVGet((char*)"video");

    if (phash == "") {
        const char* msg = "reading resource part video";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_video",  "part_hash", phash.c_str());
    /* Read the part in memory */
    uint32_t psz = 0;
    uint64_t max_size = 39 * 1024 * 1024; // FIXME - need ability to stream result
    auto body = ctx->QReadPart(phash.c_str(), 0, max_size, &psz);
    if (body->size() == 0) {
        const char* msg  = "Failed to read resource part";
        LOG_ERROR(ctx, msg, "HASH", phash.c_str());
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_video webisod.video",  "part_size", (int)psz);

    auto res = ctx->WriteOutput(*body);
    if (res.second.IsError()){
        LOG_ERROR(ctx, res.second.getJSON());
        return ctx->make_error(res.first,res.second);
    }
    return ctx->make_success();
}

/*
 * Outputs the data from key "image"
 */
elv_return_type make_image(BitCodeCallContext* ctx){
    auto phash = ctx->KVGet((char*)"image");
    if (phash == "") {
        const char* msg = "Failed to read key";
        LOG_ERROR(ctx, msg, "KEY", "image");
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_image thumbnail",  "part_hash", phash.c_str());

    /* Read the part in memory */
    uint32_t psz = 0;
    uint64_t max_size = 39 * 1024 * 1024; // FIXME - need ability to stream result
    auto body = ctx->QReadPart(phash.c_str(), 0, max_size, &psz);
    if (body->size() == 0) {
        const char* msg  = "Failed to read resource part";
        LOG_ERROR(ctx, msg, "HASH", phash.c_str());
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_image webisod.video",  "part_size", (int)psz);

    auto res = ctx->WriteOutput(*body);
    if (res.second.IsError()){
        const char* msg  ="write output failed";
        return ctx->make_error(msg, res.second);
    }
    return ctx->make_success();
}

/*
  Renders an html page with video from /rep/video
*/
elv_return_type make_html(BitCodeCallContext* ctx, char *url){
  size_t szUrl = strlen(url) + 1;
  char videoUrl[szUrl+5];
  strcpy(videoUrl,url);

  char *res = strrchr(videoUrl, '/');
  if(strcmp(res,"/html") != 0){
    const char* msg  = "bad format to url missing .html";
    return ctx->make_error(msg, E(msg).Kind(E::Invalid));
  }
  /* replace html with video */
  if (res != NULL)
      strcpy(res, "/video");

   /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = (char*)"<!doctype html>" \
        "<html>" \
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
    if (write_res.second.IsError()){
        return ctx->make_error("write output failed", write_res.second);
    }
    return ctx->make_success();
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
elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }
    char* pContentRequest = (char*)(path.first.c_str());


    if (strcmp(pContentRequest, "/video") == 0)  // really need to return error if not matching any
      return make_video(ctx);
    //else if (strcmp(pContentRequest, "/html") == 0)
      //return make_html(ctx, url);
    else if (strcmp(pContentRequest, "/image") == 0)
      return make_image(ctx);
    else{
        const char* msg  ="unknown  service requested must be one of /hmtl /video or /image";
        return ctx->make_error(msg, E(msg).Kind(E::Invalid));
    }
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
elv_return_type validate(BitCodeCallContext* ctx, JPCParams& p){
    int found = cddl_parse_and_check(ctx, cddl);

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/cddl_num_mandatories);

    std::vector<uint8_t> vec(valid_pct, valid_pct + 4);
    auto res = ctx->WriteOutput(vec);
    if (res.second.IsError()){
        return ctx->make_error("write output failed", res.second);
    }
    return ctx->make_success();
}


BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(validate)
END_MODULE_MAP()



#ifdef UNIT_TEST

char *QReadPart(char *a3, uint32_t o, uint32_t s, uint32_t *a4)  { return "NOT IMPLEMENTED"; }
char *KVGet(char *a3)                    { return "NOT IMPLEMENTED"; }
char *KVList(char *a1, char *a2)                             { return "NOT IMPLEMENTED"; }

int main(int argc, char *argv[])
{
    printf("CDDL:\n%s\n", cddl);
    cddl_parse_and_check(cddl, "", "");
    return 0;
}

#endif
