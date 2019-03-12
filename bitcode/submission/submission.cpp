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
#include <assert.h>
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/cddl.h"
#include "eluvio/bitcode_context.h"

using namespace elv_context;

/*
 *  Outputs mp4 from key 'webisode.video' with content type video/mp4
 */
elv_return_type make_video(BitCodeCallContext* ctx){
    auto phash = ctx->KVGet((char*)"webisode.video");
    if (phash == "") {
        const char* msg = "Failed to read resource part webisode.video";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_video", "webisode.video part_hash", phash.c_str());

    /* Read the part in memory */
    uint32_t psz = 0;
    uint64_t max_size = 39 * 1024 * 1024; // FIXME - need ability to stream result
    auto body = ctx->QReadPart(phash.c_str(), 0, max_size, &psz);
    if (body->size() == 0) {
        const char* msg = "Failed to read resource part HASH";
        LOG_ERROR(ctx, msg, "HASH", phash.c_str())
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_video",  "webisod.video part_size", (int)psz);

    auto res = ctx->WriteOutput(*body);
    if (res.second.IsError()){
        return ctx->make_error("write output failed", res.second);
    }
    return ctx->make_success();
}

/*
 * Outputs PDF from key 'contract'
 */
elv_return_type make_pdf(BitCodeCallContext* ctx)
{
    auto phash = ctx->KVGet((char*)"contract");

    if (phash == "") {
        const char* msg = "Failed to read key contract";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_pdf contract", "part_hash", phash.c_str());

    /* Read the part in memory */
    uint32_t psz = 0;
    auto body = ctx->QReadPart(phash.c_str(), 0, -1, &psz);
    if (body->size() == 0) {
        const char* msg = "Failed to read resource part";
        LOG_ERROR(ctx, msg, "HASH", phash.c_str());
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    LOG_INFO(ctx, "make_pdf contract",  "part_size", (int)psz);

    auto res = ctx->WriteOutput(*body);
    if (res.second.IsError()){
        return ctx->make_error(res.first, res.second);
    }
    return ctx->make_success();
}

/*
  Renders an html page with a dash player.
*/
elv_return_type make_html(BitCodeCallContext* ctx, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char mp4Url[szUrl];
  strcpy(mp4Url,url);

  char *ext = strrchr(mp4Url, '.');
  if(strcmp(ext,".html") != 0){
      const char* msg = "bad format to url missing .html";
      return ctx->make_error(msg, E(msg).Kind(E::Other));
  }
  /* replace html with mpd */
  if (ext != NULL)
      strcpy(ext, ".mp4");

    /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = (char*) "<!doctype html>" \
        "<html>" \
            "<head>" \
                "<title>Eluvio Fabric Player</title>" \
                "<style>" \
                    "video {" \
                        "width: 100%%;" \
                        "height: 100%%;" \
                    "}" \
                "</style>" \
            "</head>" \
            "<body>" \
                "<div>" \
                    "<video autoplay controls>" \
                    "<source src=\"%s\" type=\"video/mp4\">"
                    "</video>" \
                "</div>" \
            "</body>" \
        "</html>";

    /* Build html page with mpd source */
    int szBody = strlen(bodyFormat) + strlen(mp4Url) + 1;
    char body[szBody];
    snprintf(body,szBody,bodyFormat,mp4Url);

    /* Prepare output */
    std::vector<uint8_t> vec(body, body + strlen(body));
    auto res = ctx->WriteOutput(vec);
    if (res.second.IsError()){
        return ctx->make_error("write output failed", res.second);
    }
    return ctx->make_success();
}

/*
 * Dispatch content requests
 *
 * Arguments:
 *   qlibid       - content library ID
 *   qhash        - content 'hash'
 *   URL path (e.g.: "/dash/EN-1280x544-1300000-init.m4v")
 *
 * Example URLs:
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/content.pdf
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/content.mp4
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/content.html
 */
elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }

    typedef enum eMKTypes { VIDEO , HTML, PDF } MKTypes;

    char* pContentRequest = (char*)(path.first.c_str());
    MKTypes mk;

    // int szUrl = 4*1024;
    // char url[szUrl];
    // /*FIXME: Need a way to get the url instead of localhost:8008 */
    // snprintf(url, szUrl, "http://localhost:8008/qlibs/%s/q/%s/rep%s",qlibid,qhash,pContentRequest);

    char *dot = strrchr(pContentRequest, '.');
    if (!dot){
        const char* msg = "libid not provided";
        return ctx->make_error(msg, E(msg).Kind(E::Invalid));
    }
    if (strcmp(dot, ".mp4") == 0)  // really need to return error if not matching any
        mk = VIDEO;
    else if (strcmp(dot, ".html") == 0)
        mk = HTML;
    else if (strcmp(dot, ".pdf") == 0)
        mk = PDF;
    else{
        const char* msg = "libid not provided";
        return ctx->make_error(msg, E(msg).Kind(E::Invalid));
    }

    switch(mk){
    case VIDEO:
        return make_video(ctx);
    case HTML:
        //return make_html(ctx,url);
    case PDF:
        return make_pdf(ctx);
    default:
        {
        const char* msg  = "default case hit unexpectedly";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
        }
    };

    const char* msg  = "default RETURN hit unexpectedly";
    return ctx->make_error(msg, E(msg).Kind(E::Other));
}

int cddl_num_mandatories = 17;
char *cddl = (char*)"{"
    "\"webisode.metadata.title\" : bytes,"
    "\"webisode.metadata.synopsis\" : text,"
    "\"webisode.metadata.cast_and_crew\" : text,"
    "\"webisode.metadata.title\" : text,"
    "\"webisode.blog-post\" : text,"
    "\"webisode.facebook.text.1\" : text,"
    "\"webisode.facebook.text.2\" : text,"
    "\"webisode.facebook.proxy.1min\" : eluv.video,"
    "\"webisode.youtube.title.1\" : text,"
    "\"webisode.youtube.title.2\" : text,"
    "\"webisode.youtube.description\" : text,"
    "\"webisode.video\" : eluv.video,"
    "\"webisode.still.highres.1\" : eluv.img,"
    "\"webisode.still.highres.2\" : eluv.img,"
    "\"webisode.tags\" : [* bytes],"
    "\"contract\" : eluv.pdf,"
    "\"addendum\": restricted.text"
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

char *QReadPart(char *a3,
    uint32_t o, uint32_t s, uint32_t *a4)  { return "NOT IMPLEMENTED"; }
char *KVGet( char *a3)                    { return "NOT IMPLEMENTED"; }
char *KVList(char *a1, char *a2)                             { return "NOT IMPLEMENTED"; }

int main(int argc, char *argv[])
{
    printf("CDDL:\n%s\n", cddl);
    cddl_parse_and_check(cddl, "", "");
    return 0;
}

#endif
