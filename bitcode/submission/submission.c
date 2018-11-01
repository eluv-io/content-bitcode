#ifndef __WASMCEPTION_H
#define __WASMCEPTION_H

#define WASM_EXPORT __attribute__ ((visibility ("default")))

#endif // __WASMCEPTION_H

#include "qspeclib.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

/*
 *  Outputs mp4 from key 'webisode.video' with content type video/mp4
 */
int
make_video(int outsz, char *outbuf, char* qlibid, char* qhash)
{
    char *headers =
        "Content-Type: video/mp4\n\r";

    char* phash = KVGet(qlibid, qhash, "webisode.video");
    if (phash == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-SUBMISSION webisode.video part_hash=%s\n", phash);

    /* Read the part in memory */
    uint32_t psz = 0;
    char *body = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    if (body == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-SUBMISSION webisod.video part_size=%d\n", (int)psz);

    /* Prepare output */
    char *outargv[2];
    uint32_t outargsz[2];
    outargv[0] = headers;
    outargsz[0] = strlen(headers);
    outargv[1] = body;
    outargsz[1] = psz;
    argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);

    return 0;
}

/*
 * Outputs PDF from key 'contract'
 */
int
make_pdf(int outsz, char *outbuf, char* qlibid, char* qhash)
{
    char *headers =
        "Content-Type: application/pdf\n\r";

    char* phash = KVGet(qlibid, qhash, "contract");
    if (phash == NULL) {
        printf("Failed to read key\n");
        return -1;
    }
    printf("DBG-SUBMISSION contract part_hash=%s\n", phash);

    /* Read the part in memory */
    uint32_t psz = 0;
    char *body = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    if (body == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-SUBMISSION contract part_size=%d\n", (int)psz);

    /* Prepare output */
    char *outargv[2];
    uint32_t outargsz[2];
    outargv[0] = headers;
    outargsz[0] = strlen(headers);
    outargv[1] = body;
    outargsz[1] = psz;
    argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);

    return 0;
}

/*
  Renders an html page with a dash player.
*/
int
make_html(int outsz, char *outbuf, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char mp4Url[szUrl];
  strcpy(mp4Url,url);

  char *ext = strrchr(mp4Url, '.');
  if(strcmp(ext,".html") != 0)
    return -1;

  /* replace html with mpd */
  if (ext != NULL)
      strcpy(ext, ".mp4");

    char *headers =
        "Content-Type: text/html\n\r";

    /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = "<!doctype html>" \
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
    char *outargv[2];
    uint32_t outargsz[2];
    outargv[0] = headers;
    outargsz[0] = strlen(headers);
    outargv[1] = body;
    outargsz[1] = strlen(body);
    argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);

    return 0;
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
int
content(int outsz, char *outbuf, const char *argbuf)
{
    typedef enum eMKTypes { VIDEO , HTML, PDF } MKTypes;

    int argc;
    char **argv;
    uint32_t *argsz;
    int res = 0;

    argc = buf2argv(argbuf, &argv, &argsz);

    // argc should be 3
    if (argc < 3) return -1;
    printf("argc=%d lib=%s url=%s\n", argc, argv[0], argv[2]);

    char *qlibid = argv[0];
    char *qhash = argv[1];
    char* pContentRequest = argv[2];
    MKTypes mk;

    int szUrl = 4*1024;
    char url[szUrl];
    /*FIXME: Need a way to get the url instead of localhost:8008 */
    snprintf(url, szUrl, "http://localhost:8008/qlibs/%s/q/%s/rep%s",qlibid,qhash,pContentRequest);

    char *dot = strrchr(pContentRequest, '.');
    if (!dot)
      return -1;

    if (strcmp(dot, ".mp4") == 0)  // really need to return error if not matching any
        mk = VIDEO;
    else if (strcmp(dot, ".html") == 0)
        mk = HTML;
    else if (strcmp(dot, ".pdf") == 0)
        mk = PDF;
    else
        return -1;

    switch(mk){
    case VIDEO:
        make_video(outsz, outbuf, qlibid, qhash);
        break;
    case HTML:
        make_html(outsz, outbuf, url);
        break;
    case PDF:
        make_pdf(outsz, outbuf, qlibid, qhash);
        break;
    default:
        res = -1;
        break;
    };
    freeargv(argc, argv, argsz);
    return res;
}

int cddl_num_mandatories = 17;
char *cddl = "{"
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
int
validate_old(
    int outsz,
    char *outbuf,
    const char *argbuf)
{
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc < 2) {
        freeargv(argc, argv, argsz);
        return -1;
    }

    char *qlibid = argv[0];
    char *qwt = argv[1];

    /* List all components */
    char *mdbuf = KVList(qlibid, qwt);
    if (mdbuf == NULL) {
        freeargv(argc, argv, argsz);
        return -1;
    }

    /* Iterate through all components */
    int mdargc = 0;
    char **mdargv = NULL;
    uint32_t *mdargsz = NULL;
#if NECESSARY
    mdargc = buf2argv(mdbuf, &mdargv, &mdargsz);
    if (mdargc < 0) {
        freeargv(argc, argv, argsz);
        return -1;
    }

    for (int i = 0; i < mdargc; i ++) {
        if (strstr(mdargv[i], "webisode") != NULL) {
            char *phash = KVGet(qlibid, qwt, mdargv[i]);
            printf("COMPONENT %s => %s\n", mdargv[i], phash ? phash : "-");
        }
    }
#endif

    const int num_mandatories = 6;
    char *mandatories[num_mandatories] = {
        "webisode.video",
        "webisode.blog-post",
        "webisode.facebook.text.1",
        "webisode.facebook.text.2",
        "webisode.youtube.title.1",
        "webisode.youtube.title.2"
    };

    assert(num_mandatories < 256);
    char found = 0;
    for (int i = 0; i < num_mandatories; i ++) {
        printf("CHECK %d %s\n", i, mandatories[i]);
        char *val = KVGet(qlibid, qwt, mandatories[i]);
        if (val != NULL && strlen(val) > 0) {
            found ++;
        }
    }

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/num_mandatories);

    /* Prepare output */
    char *outargv[5];
    uint32_t outargsz[5];
    outargv[0] = "application/json";
    outargsz[0] = strlen(outargv[0]);
    outargv[1] = "valid_pct";
    outargsz[1] = strlen(outargv[1]);
    outargv[2] = valid_pct;
    outargsz[2] = strlen(outargv[2]);;
    outargv[3] = "proof";
    outargsz[3] = strlen(outargv[3]);
    outargv[4] = "[SIGNED PROOF]";
    outargsz[4] = strlen(outargv[4]);
    argv2buf(5, (const char **)outargv, outargsz, outbuf, outsz);

    if (argc) freeargv(argc, argv, argsz);
    if (mdargc) freeargv(mdargc, mdargv, mdargsz);

    return 0;
}


/*
 * Validate content components.
 *
 * Returns:
 *  -1 in case of unexpected failure
 *   0 if valid
 *  >0 the number of validation problems (i.e. components missing or wrong)
 */
int
validate(
    int outsz,
    char *outbuf,
    const char *argbuf)
{
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc < 2) {
        freeargv(argc, argv, argsz);
        return -1;
    }

    char *qlibid = argv[0];
    char *qhash = argv[1];

    int found = cddl_parse_and_check(cddl, qlibid, qhash);

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/cddl_num_mandatories);

    /* Prepare output */
    char *outargv[5];
    uint32_t outargsz[5];
    outargv[0] = "application/json";
    outargsz[0] = strlen(outargv[0]);
    outargv[1] = "valid_pct";
    outargsz[1] = strlen(outargv[1]);
    outargv[2] = valid_pct;
    outargsz[2] = strlen(outargv[2]);;
    outargv[3] = "proof";
    outargsz[3] = strlen(outargv[3]);
    outargv[4] = "[SIGNED PROOF]";
    outargsz[4] = strlen(outargv[4]);
    argv2buf(5, (const char **)outargv, outargsz, outbuf, outsz);

    if (argc) freeargv(argc, argv, argsz);

    return 0;
}

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
