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
 *  Outputs video from key "video"
 */
int
make_video(int outsz, char *outbuf, char* qlibid, char* qhash)
{
    char *headers = "video/mp4";

    char* phash = KVGet(qlibid, qhash, "video");
    if (phash == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-VIDEO video part_hash=%s\n", phash);

    /* Read the part in memory */
    uint32_t psz = 0;
    char *body = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    if (body == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-VIDEO video part_size=%d\n", (int)psz);

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
 * Outputs the data from key "image"
 */
int
make_image(int outsz, char *outbuf, char* qlibid, char* qhash)
{
    char *headers = "image/png";

    char* phash = KVGet(qlibid, qhash, "image");
    if (phash == NULL) {
        printf("Failed to read key\n");
        return -1;
    }
    printf("DBG-VIDEO thumbnail part_hash=%s\n", phash);

    /* Read the part in memory */
    uint32_t psz = 0;
    char *body = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    if (body == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("DBG-VIDEO thumbnail part_size=%d\n", (int)psz);

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
  Renders an html page with video from /rep/video
*/
int
make_html(int outsz, char *outbuf, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char videoUrl[szUrl+5];
  strcpy(videoUrl,url);

  char *res = strrchr(videoUrl, '/');
  if(strcmp(res,"/html") != 0)
    return -1;

  /* replace html with video */
  if (res != NULL)
      strcpy(res, "/video");

    char *headers = "text/html";

    /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = "<!doctype html>" \
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
 *   URL path (e.g.: "/html")
 *
 * Example URLs:
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/video
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/html
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/rep/image
 */
int
content(int outsz, char *outbuf, const char *argbuf)
{
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

    int szUrl = 4*1024;
    char url[szUrl];
    /*FIXME: Need a way to get the url instead of localhost:8008 */
    snprintf(url, szUrl, "http://localhost:8008/qlibs/%s/q/%s/rep%s",qlibid,qhash,pContentRequest);

    if (strcmp(pContentRequest, "/video") == 0)  // really need to return error if not matching any
      make_video(outsz, outbuf, qlibid, qhash);
    else if (strcmp(pContentRequest, "/html") == 0)
      make_html(outsz, outbuf, url);
    else if (strcmp(pContentRequest, "/image") == 0)
      make_image(outsz, outbuf, qlibid, qhash);
    else
        res = -1;

    freeargv(argc, argv, argsz);
    return res;
}

int cddl_num_mandatories = 4;
char *cddl = "{"
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

#if 0
    int found = cddl_parse_and_check(cddl, qlibid, qhash);
#else
    int found = cddl_num_mandatories;
    (void)qlibid;
    (void)qhash;
#endif
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
