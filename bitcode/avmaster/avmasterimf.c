/*
 * AVMASTER.IMF - custom content type
 *
 * Implements the AVMASTER specific API.
 *
  resources -> [ RID1, RID2, ... ]
  resource.{RID}.name      -> name
  resource.{RID}.parthash  -> part hash
  resource.{NAME}.parthash -> part hash

  temp.resource.{RID}.name  -> name
  temp.resource.{RID}.parthash -> part hash

  buildspecs ->  [ BID1, BID2, ... ]
  buildspec.{BID}.name     ->  name
  buildspec.{BID}.content  ->  buildspec contents (XML)

  vidformats.width -> [ W1, W2, ... ]
  vidformats.height -> [ H1, H2, ... ]
  vidformats.bw     -> [ BW1, BW2, ...]
 *
 */

/*
  Build commands:

  /usr/local/opt/llvm/bin/clang -O0 -I .. -Wall -emit-llvm -c avmasterimf.c fixup.c ../qspeclib.c
  /usr/local/opt/llvm/bin/llvm-link -v avmasterimf.bc fixup.bc qspeclib.bc -o avmaster.imf.bc
*/

#ifndef __WASMCEPTION_H
#define __WASMCEPTION_H

#define WASM_EXPORT __attribute__ ((visibility ("default")))

#endif // __WASMCEPTION_H

#include "qspeclib.h"
#include "fixup.h"

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

#define CLEANUP_FILES 1

char* rootDir = "./temp";

/* Core and Extended API */
extern int64_t FFMPEGRun(char*, int, char*, int);
extern void LOGMsg(char *);
extern int KVSet(char *, char *, char *, char *);
extern char *KVGetTemp(char *, char *, char *);
extern int KVPushBack(char *, char *, char *, char *);
extern char *KVRangeTemp(char *, char *, char *);
extern char *QCreatePart(char *, char *, char *, uint32_t);
extern char *QReadPart(char *, char *, char *, uint32_t, uint32_t, uint32_t *);
extern char *KVGet(char *, char *, char *);
extern char *KVRange(char *, char *, char *);

char *szCPL = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?> " \
"<CompositionPlaylist>" \
"  <Id>AAAAAAAA-0000-0000-0000-111111111111</Id><!-- Normally matches filename: CPL_AAAAAAAA-0000-0000-0000-111111111111.xml -->" \
"  <Annotation>EN - DASH - Star Wars</Annotation>" \
"  <SegmentList>" \
"    <Segment>" \
"      <Id>BBBBBBBB-0000-0000-0000-111111111111</Id>" \
"      <SequenceList>" \
"        <MainImageSequence>" \
"         <Id>CCCCCCCC-0000-0000-0000-111111111111</Id>" \
"          <ResourceList>" \
"            <Resource>" \
"              <Id>DDDDDDDD-0000-0000-0000-111111111111</Id>" \
"              <Annotation>EN_video_master.mp4</Annotation>" \
"              <EditRate>24000 1001</EditRate><!-- 1 time unit = (1001/24000) seconds -->" \
"              <EntryPoint>0</EntryPoint>" \
"              <SourceDuration>7193</SourceDuration>" \
"            </Resource>" \
"          </ResourceList>" \
"        </MainImageSequence>" \
"        <MainAudioSequence>" \
"         <Id>CCCCCCCC-0000-0000-0000-222222222222</Id>" \
"          <ResourceList>" \
"            <Resource>" \
"              <Id>DDDDDDDD-0000-0000-0000-222222222222</Id>" \
"              <Annotation>EN_audio_stereo.aac</Annotation>" \
"              <EditRate>48000 1</EditRate><!-- 1 time unit = (1/48000) seconds -->" \
"              <EntryPoint>0</EntryPoint>" \
"              <SourceDuration>14400386</SourceDuration>" \
"            </Resource>" \
"          </ResourceList>" \
"        </MainAudioSequence>" \
"      </SequenceList>" \
"    </Segment>" \
"  </SegmentList>" \
"</CompositionPlaylist>";


static int packStringArray( char* inputBuffer, const char* const * arrayToPack, int stringCount){
    uint32_t* begin = (uint32_t *)inputBuffer;
    inputBuffer += sizeof(int);  //skip over size of struct
    int bufLen = sizeof(int);
    for (int i = 0; i < stringCount; i++){
        const char* curString = arrayToPack[i];
        uint32_t len = strlen(curString);
         *((uint32_t*)(inputBuffer)) = htonl(len);
        inputBuffer += sizeof(int);
        strcpy(inputBuffer, curString);
        inputBuffer += len;
        bufLen += len + sizeof(int);
    }
    *((uint32_t*)(begin)) = htonl(stringCount);
    return bufLen;
}

/* Write out a 'resource' as a file for ffmpeg to use as input */
static int
resourceToFile(char *qlibid, char* qhash, char *resname, char *outdir)
{
    char keybuf[1024];
    sprintf(keybuf, "resource.%s.parthash", resname);
    char *phash = KVGet(qlibid, qhash, keybuf);
    if (phash == NULL) {
        printf("Failed to read resource part hash\n");
        return -1;
    }
    //printf("resourceToFile name=%s phash=%s\n", resname, phash ? phash : "NULL");

    /* Read the part in memory */
    uint32_t psz = 0;
    char *c = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    if (c == NULL) {
        printf("Failed to read resource part\n");
        return -1;
    }
    printf("resourceToFile readpart sz=%d\n", psz);

    /* Write the part to file */
    char *fname = (char *)alloca(strlen(outdir) + 1 + strlen(resname));
    sprintf(fname, "%s/%s", outdir, resname);
    int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Failed to open temp file\n");
        return -1;
    }
    int rc = write(fd, c, psz);
    free(c);
    if (rc < psz) {
        printf("Failed to write temp file\n");
        return -1;
    }
    close(fd);
    return 0;
}

/*
 * Start writing a resource with the specified ID and name. Requires a content
 * object previously open for writing (and its 'write token').
 *
 * Arguments:
 *   qlibid string  - content library ID
 *   wrtoken string - content 'write token'
 *   id string      - the ID of the resource
 *   name string    - the name of the resource
 *   contents bytes - the contents of the part
 * Return:
 *   part write token
 *
 * Notes:
 *  - there is also a longer textual description in IMF CPL
 *
 */
int
resourceWrite(int outsz, char *outbuf, const char *argbuf)
{
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc < 5)
        return -1;

    char *qlibid = argv[0];
    char *wrtoken = argv[1];
    char *id = argv[2];
    char *name = argv[3];
    char *contents = argv[4]; // not null terminated
    uint32_t contentsz = argsz[4];

    //printf("argc=%d qlibid=%s qwt=%s resid=%s resname=%s csz=%d\n",
    //    argc, argv[0], argv[1], argv[2], argv[3], argsz[4]);

    int rc;
    const int keysz = 1024;
    char keybuf[keysz];

    // creates and finalizes the part
    char *parthash = QCreatePart(qlibid, wrtoken, contents, contentsz);
    if (parthash == NULL)
        return -1;

    // add id to resources list
    rc = KVPushBack(qlibid, wrtoken, "resources", id);
    //printf("KVPushBack res=%d\n", rc);

    (void)snprintf(keybuf, keysz, "resource.%s.name", id);
    rc = KVSet(qlibid, wrtoken, keybuf, name);
    //printf("KVSet res=%d\n", rc);

    (void)snprintf(keybuf, keysz, "resource.%s.parthash", id);
    rc = KVSet(qlibid, wrtoken, keybuf, parthash);
    //printf("KVSet res=%d\n", rc);

    (void)snprintf(keybuf, keysz, "resource.%s.parthash", name);
    rc = KVSet(qlibid, wrtoken, keybuf, parthash);
    //printf("KVSet res=%d\n", rc);

    /* Prepare output */
    char *outargv[1];
    uint32_t outargsz[1];
    outargv[0] = parthash;
    outargsz[0] = strlen(parthash);
    argv2buf(1, (const char **)outargv, outargsz, outbuf, outsz);
    freeargv(argc, argv, argsz);

    return 0;
}

/*
 * Arguments:
 *   qlibid
 *   wrtoken
 * Return:
 *   list of resources ID, name and descriptions
 */
int
resourceList(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc < 2)
        return -1;

    char *qlibid = argv[0];
    char *qhash = argv[1];
    const int keysz = 128;
    char keybuf[keysz];

    // load all elements in resources list
    char *elems = KVRange(qlibid, qhash, "resources");
    if (elems == NULL) elems = strdup("");
    int nelems = 0;
    char **elemv = NULL;
    uint32_t *elemsz = NULL;
    nelems = buf2argv(elems, &elemv, &elemsz);

    char *outargv[3 * nelems];
    uint32_t outargsz[3 * nelems];
    // iterate over elements and write to output buffer
    for (int i = 0; i < nelems; i ++) {
        // printf("Elem %d = %s\n", i, elemv[i]);
        char *id = elemv[i];
        // lookup this id's name, parthash in the resources
        (void)snprintf(keybuf, keysz, "resource.%s.name", id);
        char* name = KVGet(qlibid, qhash, keybuf);
        if (name == NULL) name = strdup("");
        (void)snprintf(keybuf, keysz, "resource.%s.parthash", id);
        char* parthash = KVGet(qlibid, qhash, keybuf);
        if (parthash == NULL) parthash = strdup("");
        outargv[3 * i] = id;
        outargsz[3 * i] = strlen(id);
        outargv[3 * i + 1] = name;
        outargsz[3 * i + 1] = strlen(name);
        outargv[3 * i + 2] = parthash;
        outargsz[3 * i + 2] = strlen(parthash);
    }
    argv2buf(3 * nelems, (const char **)outargv, outargsz, outbuf, outsz);
    freeargv(argc, argv, argsz);
    return 0;
}

int
buildspecWrite(int outsz, char *outbuf, const char *argbuf)
{
    int argc;
    char **argv;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);
    //printf("argc=%d arg0=%s\n", argc, argv[0]);

    int rc;
    const int keysz = 128;
    char keybuf[keysz];

    if (argc < 1)
        return -1;

    char *qlibid = argv[0];
    char *wrtoken = argv[1];
    char *id = argv[2];
    char *name = argv[3];
    char *data = argv[4];

    // set buildspec.{ID}.name -> name
    // set buildspec.{ID}.contents -> the contents data
    // add to 'buildspecs' list the ID

    rc = KVPushBack(qlibid, wrtoken, "buildspecs", id);
    //printf("KVPushBack res=%d\n", rc);

    (void)snprintf(keybuf, keysz, "buildspec.%s.name", id);
    rc = KVSet(qlibid, wrtoken, keybuf, name);

    (void)snprintf(keybuf, keysz, "buildspec.%s.contents", id);
    rc = KVSet(qlibid, wrtoken, keybuf, data);

    // No output buffer to return
    return 0;
}
/*
 * Arguments:
 *   qlibid string - the content library
 *   qhash string - the content hash
 * Return:
 *   list of buildspecs ID, name and descriptions
 */
int
buildspecList(int outsz, char *outbuf, const char *argbuf)
{

    // read function args
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc < 2)
        return -1;

    char *qlibid = argv[0];
    char *qhash = argv[1];
    const int keysz = 128;
    char keybuf[keysz];

    // load all elements in buildspecs list
    char *elems = KVRange(qlibid, qhash, "buildspecs");
    if (elems == NULL) elems = strdup("");
    int nelems = 0;
    char **elemv = NULL;
    uint32_t *elemsz = NULL;
    nelems = buf2argv(elems, &elemv, &elemsz);

    char *outargv[3 * nelems];
    uint32_t outargsz[3 * nelems];
    // iterate over elements and write to output buffer
    for (int i = 0; i < nelems; i ++) {
        // printf("Elem %d = %s\n", i, elemv[i]);
        char *id = elemv[i];
        // lookup this id's name, parthash in the resources
        (void)snprintf(keybuf, keysz, "buildspec.%s.name", id);
        char* name = KVGet(qlibid, qhash, keybuf);
        if (name == NULL) name = strdup("");
        (void)snprintf(keybuf, keysz, "buildspec.%s.contents", id);
        char* contents = KVGet(qlibid, qhash, keybuf);
        if (contents == NULL) contents = strdup("");
        outargv[3 * i] = id;
        outargsz[3 * i] = strlen(id);
        outargv[3 * i + 1] = name;
        outargsz[3 * i + 1] = strlen(name);
        outargv[3 * i + 2] = contents;
        outargsz[3 * i + 2] = strlen(contents);
    }
    argv2buf(3 * nelems, (const char **)outargv, outargsz, outbuf, outsz);
    freeargv(argc, argv, argsz);
    return 0;
}

static char*
getCPLByName(char* qlibid, char* qhash, char *subname, char* retbuf) {

    // get the list of CPLs like getting list of formats
    // iterate over the list to get the buildspec id,
    // construct the name key string from the id
    // look up the name string by this key
    // test whether the full name contains this subname
    // if so, this is our id & return the contents

    int rc;
    char foutbuf[100*1024];

    // construct argbuf to input
    int nelems = 2;
    char *argv[nelems];
    uint32_t argsz[nelems];
    char buf[100*1024];
    uint32_t sz = 0;;
    argv[0] = qlibid;
    argv[1] = qhash;
    argsz[0] = strlen(qlibid);
    argsz[1] = strlen(qhash);

    argv2buf(nelems, (const char **)argv, argsz, buf, sz);

    rc = buildspecList(sizeof(foutbuf), foutbuf, buf);
    if (rc < 0) {
        printf("Failed formatsGet rc=%d\n", rc);
        // FIXME cleanup and return
    }
    // unpack the out buffer
    char **fargv;
    uint32_t *fargsz;
    int foutsz = buf2argv(foutbuf, &fargv, &fargsz);
    char *cpldata = retbuf;
    // list is id, name, contents
    for (int i = 0; i < foutsz/3; i++) {
        char* name = fargv[i*3+1];
        //printf("CPL id=%s %s\n", fargv[i * 3], name);
        // does name contain subname?
        // e.g. name = CPL_EN.xml, subname = EN
        if ( strstr(name,subname) != NULL ) {
            memcpy(cpldata, fargv[i*3+2], fargsz[i * 3 + 2]);
            break;
        }
    }
    freeargv(foutsz, fargv, fargsz);
    return cpldata;
}


/*
 * Arguments:
 *   qlibid string - the content library id
 *   wrtoken string - the content writetoken
 *   formats array - the w1,h1,bw1 w2,h2,bw2, ... wn,hn,bwn
 * Return:
 *   none
 */
int
formatsSet(int outsz, char *outbuf, const char *argbuf)
{

    //int rc = 0;
    const int keysz = 128;
    char keybuf[keysz];

    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);
    //printf("argc=%d arg0=%s\n", argc, argv[0]);

    /* Todo - assert argc - 2 mod 3 = 0 */
    if (argc < 5)
        return -1;

    char* qlibid = argv[0];
    char* wrtoken = argv[1];

    // Add this CPL name to the vidformat list
    //rc = KVPushBack(qlibid, wrtoken, "vidformats", name);
    //printf("KVPushBack res=%d\n", rc);

    // Add the w,h,bw to their respective vectors
    for (int i=2; i<argc; i++) {
        (void)snprintf(keybuf, keysz, "vidformats.width");
        KVPushBack(qlibid, wrtoken, keybuf, argv[i]);
        i++;
        (void)snprintf(keybuf, keysz, "vidformats.height");
        KVPushBack(qlibid, wrtoken, keybuf, argv[i]);
        i++;
        (void)snprintf(keybuf, keysz, "vidformats.bw");
        KVPushBack(qlibid, wrtoken, keybuf, argv[i]);
    }
    return 0;
}

/*
 * Return the list of formats.
 *
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - the content hash (content must be finalized)
 *   formats array - the w1,h1,bw1 w2,h2,bw2, ... wn,hn,bwn
 *
 * Return:
 *   list of 'formats' as: w1, h1, bw1, w2, h2, bw2, ...
 */
int
formatsGet(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);

    if (argc < 2) {
        if (argv) freeargv(argc, argv, argsz);
        return -1;
    }

    char *qlibid = argv[0];
    char *qhash = argv[1];

    // load all elements in formats lists
    int nwelems = 0;
    int nhelems = 0;
    int nbwelems = 0;
    char **welemv = NULL;
    char **helemv = NULL;
    char **bwelemv = NULL;
    uint32_t *welemsz = NULL;
    uint32_t *helemsz = NULL;
    uint32_t *bwelemsz = NULL;

    char *width_elems = KVRange(qlibid, qhash, "vidformats.width");
    if (width_elems == NULL) width_elems = strdup("");
    nwelems = buf2argv(width_elems, &welemv, &welemsz);

    char *height_elems = KVRange(qlibid, qhash, "vidformats.height");
    if (height_elems == NULL) height_elems = strdup("");
    nhelems = buf2argv(height_elems, &helemv, &helemsz);

    char *bw_elems = KVRange(qlibid, qhash, "vidformats.bw");
    if (bw_elems == NULL) bw_elems = strdup("");
    nbwelems = buf2argv(bw_elems, &bwelemv, &bwelemsz);

    // argv no longer needed after using qlibid and qhash
    if (argv) freeargv(argc, argv, argsz);

    if (nwelems != nhelems || nwelems != nbwelems) {
        if (welemv) freeargv(nwelems, welemv, welemsz);
        if (helemv) freeargv(nhelems, helemv, helemsz);
        if (bwelemv) freeargv(nbwelems, bwelemv, bwelemsz);
        return -1;
    }

    int nelems = nhelems;
    char *outargv[3 * nelems];
    uint32_t outargsz[3 * nelems];

    // iterate over elements and write output array
    // NOTE: the return order is H, W, BW (height first)
    for (int i = 0; i < nelems; i ++) {
        // printf("formatsGet %d (h,w,bw) = %s,%s,%s\n", i, helemv[i], welemv[i], bwelemv[i]);
        char *w = welemv[i];
        char *h = helemv[i];
        char *bw = bwelemv[i];
        outargv[3 * i] = h;
        outargv[3 * i + 1] = w;
        outargv[3 * i + 2] = bw;
        outargsz[3 * i] = strlen(h);
        outargsz[3 * i + 1] = strlen(w);
        outargsz[3 * i + 2] = strlen(bw);
    }
    argv2buf(3 * nelems, (const char **)outargv, outargsz, outbuf, outsz);

    if (welemv) freeargv(nwelems, welemv, welemsz);
    if (helemv) freeargv(nhelems, helemv, helemsz);
    if (bwelemv) freeargv(nbwelems, bwelemv, bwelemsz);
    return 0;
}

char *strlwr(char *str)
{
    unsigned char *p = (unsigned char *)str;
    while (*p) {
         *p = tolower((unsigned char)*p);
          p++;
    }

    return str;
}

typedef struct tagFormatTriplet {
    char* Height;
    char* Width;
    char* Bandwidth;
} FormatTriplet;


typedef struct tagComplexPlaylist {
    int element_count;
    char** video_list;
    int* entry_points;
    int* durations;
}ComplexPlaylist;

typedef struct tagRealPlaylist {
    float start;
    float end;
    char* filename;
}RealPlaylist;



/*
 * Return a dash manifest for a given componsition (build).
 *
 * Arguments:
 *   id string - buildspec ID
 * Return:
 *   well formated DASH manifest
 */
char*
dashManifest(int outbufsize, char *outbuf, FormatTriplet* video_formats, int cFormats, char* szLang)
{
    const char* pHeader = "<?xml version=\"1.0\" encoding=\"utf-8\"?> <MPD mediaPresentationDuration='PT5M0.0S' minBufferTime='PT12.0S' profiles='urn:mpeg:dash:profile:isoff-ondemand:2011' type='static' xmlns:xlink='http://www.w3.org/1999/xlink' xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xmlns='urn:mpeg:dash:schema:mpd:2011' xsi:schemaLocation='urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd'> <ProgramInformation></ProgramInformation>   <Period start='PT0.0S'>";

    const char* pFormatHeader = "<AdaptationSet bitstreamSwitching='true' contentType='video' lang='%s' segmentAlignment='true'> <Role schemeIdUri='urn:mpeg:dash:role:2011' value='main'></Role>";

    const char* pFormat = "<Representation bandwidth='%s' codecs='avc1.64001f' frameRate='24000/1001' height='%s' id='%sx%s-%s' mimeType='video/mp4' width='%s'> <SegmentTemplate duration='6006000' initialization='%s-$RepresentationID$-init.m4v' media='%s-$RepresentationID$-$Number$.m4v' startNumber='1' timescale='1000000'></SegmentTemplate> </Representation>";

    const char* pFormatFooter = "</AdaptationSet>  <AdaptationSet bitstreamSwitching='true' contentType='audio' lang='%s' segmentAlignment='true'> <Representation audioSamplingRate='48000' bandwidth='320000' codecs='mp4a.40.2' id='stereo-320000' mimeType='audio/mp4'> <AudioChannelConfiguration schemeIdUri='urn:mpeg:dash:23003:3:audio_channel_configuration:2011' value='2'></AudioChannelConfiguration> <SegmentTemplate duration='6016000' initialization='%s-$RepresentationID$-init.m4a' media='%s-$RepresentationID$-$Number$.m4a' startNumber='1' timescale='1000000'></SegmentTemplate> </Representation> </AdaptationSet>";


    const char *pFooter = "</Period> </MPD>";
    char szLangLower[10];
    strcpy(szLangLower, szLang);
    strlwr(szLangLower);

    char* szOutput = outbuf;
    char szTempBuf[4096];
    char* szCurLoc = szOutput;

    const int accountForDynamicData = 50 * cFormats;

    int totalSizeRequired = strlen(pHeader) + strlen(pFormatHeader) + (cFormats * strlen(pFormat)) +
                            strlen(pFormatFooter) + accountForDynamicData;
    if (totalSizeRequired  > outbufsize){
        sprintf(outbuf, "Not enough memory for supplied buffer need at least %d", totalSizeRequired);
        return NULL;
    }

    int cbString = strlen(pHeader);
    strcpy(szCurLoc, pHeader);
    szCurLoc += cbString;
    sprintf(szTempBuf, pFormatHeader, szLangLower);
    strcpy(szCurLoc, szTempBuf);
    szCurLoc += strlen(szTempBuf);

    for (int i = 0; i < cFormats; i++){
        sprintf(szTempBuf, pFormat, video_formats[i].Bandwidth, video_formats[i].Height,
            video_formats[i].Width, video_formats[i].Height, video_formats[i].Bandwidth,
            video_formats[i].Width,szLang, szLang);
        strcpy(szCurLoc, szTempBuf);
        szCurLoc += strlen(szTempBuf);
    }
    sprintf(szTempBuf, pFormatFooter, szLangLower, szLang, szLang);
    strcpy(szCurLoc, szTempBuf);
    szCurLoc += strlen(szTempBuf);

    strcpy(szCurLoc, pFooter);

    return outbuf;
}

int
dashTest(int outsz, char *outbuf, char *url)
{
    char *headers =
        "Content-Type: video/mp4\n\r"
        "X-Headers-QFab: test\n\r";

    char *body = "NOT VIDEO AT ALL";

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
  Renders an html page with a dash player.
*/

int
dashHtml(int outsz, char *outbuf, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char mpdUrl[szUrl];
  strcpy(mpdUrl,url);

  char *ext = strrchr(mpdUrl, '.');
  if(strcmp(ext,".html") != 0)
    return -1;

  /* replace html with mpd */
  if (ext != NULL)
      strcpy(ext, ".mpd");

    char *headers =
        "Content-Type: text/html\n\r";

    /*TODO: this embedded html should be a template resource, part of the content */
    char *bodyFormat = "<!doctype html>" \
        "<html>" \
            "<head>" \
                "<title>Eluvio Fabric Dash Player</title>" \
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
                    "<video data-dashjs-player autoplay src='%s' controls>" \
                    "</video>" \
                "</div>" \
                "<script src='https://cdn.dashjs.org/latest/dash.all.min.js'></script>" \
            "</body>" \
        "</html>";

    /* Build html page with mpd source */
    int szBody = strlen(bodyFormat) + strlen(mpdUrl) + 1;
    char body[szBody];
    snprintf(body,szBody,bodyFormat,mpdUrl);

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

int Error(char* message, int code){
    printf("%s", message);
    return code;
}

int
loadFromFileAndPackData(int outsz, char* outbuf, char* filename, char* contenttype, int segno){
    int res = 0;
    char targetFilename[1024];
    sprintf(targetFilename, "%s/%s", rootDir, filename);
    FILE* targetFile = fopen(targetFilename, "rb");

    if (!targetFile){
        return Error("Error: Unable to open", -4);
    }
    fseek(targetFile, 0, SEEK_END);
    long fsize = ftell(targetFile);
    fseek(targetFile, 0, SEEK_SET);  //same as rewind(f);

    char *segData = malloc(fsize + 1);
    int cb = fread(segData, 1, fsize, targetFile);
    if (cb != fsize){
        free(segData);
        return Error("Did not read all the data", -6);
    }
    fclose(targetFile);

    if (CLEANUP_FILES) {
        if (unlink(targetFilename) < 0) {
            printf("Failed to remove temporary ffmpeg output file %s\n", targetFilename);
        }
    }

    /* Fix ffmpeg segment sequence and base time in place */
    res = ffmpegFixup(segData, fsize, segno, contenttype);
    if (res < 0) {
        free(segData);
        return Error("Failed to fix segment", -8);
    }

    /* Prepare output */
    const int sizeExpected = 2;
    char *outargv[sizeExpected];
    uint32_t outargsz[sizeExpected];
    // This assert expects the size of args to be 2 and more importantly
    // expects the second argument adjusts the pointer to the buffer
    // to skip over the first as argv2buf will write to outbuf.
    // In effect we are using the output buffer to serve as input and output
    assert(sizeExpected == 2);
    //
    outargv[0] = contenttype;
    outargsz[0] = strlen(outargv[0]);
    // If more parameters are added this pointer math must be updated
    outargv[1] = segData; // skip the null too
    outargsz[1] = fsize;
    argv2buf(sizeExpected, (const char **)outargv, outargsz, outbuf, outsz);
    free(segData);
    return res;

}

char*  getTagData(char* tag, char* tagend, char* from_buffer, char* to_buffer){
    //char* pAnnotationToken = "<Annotation>";
    char* pAnnotation = strstr(from_buffer, tag);
    if (pAnnotation == NULL){
        return NULL;
    }
    char* pCurLoc = pAnnotation + strlen(tag);
    char* pAnnotationEnd = strstr(pCurLoc, tagend);
    if (pAnnotationEnd == NULL) {
        return NULL;
    }

    strncpy(to_buffer, pCurLoc, pAnnotationEnd - pCurLoc);
    to_buffer[pAnnotationEnd - pCurLoc] = 0;
    return pAnnotationEnd + strlen(tagend);

}

int
calculateResourceCount(const char* szCPL){
    const char* pCurLoc = szCPL;
    char* pMainImgToken = "<MainImageSequence>";
    char* pMainImgTokenEnd = "</MainImageSequence>";

    char* pMainImageSeq = strstr(pCurLoc, pMainImgToken);
    if (pMainImageSeq == NULL){
        char errmsg[512];
        sprintf(errmsg, "Could not find %s in CPL\n", pMainImgToken);
        return Error(errmsg,-1);
    }
    char* pMainImageSeqEnd = strstr(pMainImageSeq, pMainImgTokenEnd);
     if (pMainImageSeqEnd == NULL){
        char errmsg[512];
        sprintf(errmsg, "Could not find %s in CPL\n", pMainImgTokenEnd);
        return Error(errmsg,-1);
    }
    int bufferSize = pMainImageSeqEnd - pMainImageSeq;
    char* workingBuffer = alloca(bufferSize+1);
    strncpy(workingBuffer, pMainImageSeq, bufferSize);
    workingBuffer[bufferSize+1] = 0;

    int element_count = 0;
    char* bufCurLoc = workingBuffer;
    while((bufCurLoc = strstr(bufCurLoc, "<Resource>")) != 0){
        element_count++;
        bufCurLoc += strlen("<Resource>");
    }

    return element_count;

}

int
fullParseCPL(ComplexPlaylist* plist, const char* pCurLoc){
    //ComplexPlaylist playlist;
    char* pMainImgToken = "<MainImageSequence>";
    char* pMainImgTokenEnd = "</MainImageSequence>";

    if (plist == NULL){
        return Error("Must provide storage for the list\n", -10);
    }

    char* pMainImageSeq = strstr(pCurLoc, pMainImgToken);
    if (pMainImageSeq == NULL){
        char errmsg[512];
        sprintf(errmsg, "Could not find %s in CPL\n", pMainImgToken);
        return Error(errmsg,-1);
    }
    char* pMainImageSeqEnd = strstr(pMainImageSeq, pMainImgTokenEnd);
     if (pMainImageSeqEnd == NULL){
        char errmsg[512];
        sprintf(errmsg, "Could not find %s in CPL\n", pMainImgTokenEnd);
        return Error(errmsg,-1);
    }
    int bufferSize = pMainImageSeqEnd - pMainImageSeq;
    char* workingBuffer = alloca(bufferSize+1);
    strncpy(workingBuffer, pMainImageSeq, bufferSize);
    workingBuffer[bufferSize+1] = 0;

    plist->element_count = 0;
    char* bufCurLoc = workingBuffer;
    while((bufCurLoc = strstr(bufCurLoc, "<Resource>")) != 0){
        plist->element_count++;
        bufCurLoc += strlen("<Resource>");
    }
    if (plist->element_count == 0){
        return Error("Malformed resource list, No entries", -11);
    }

    bufCurLoc = workingBuffer;
    for (int i = 0; i < plist->element_count; i++){
        char currentTagData[512];
        bufCurLoc = getTagData("<Annotation>", "</Annotation>", bufCurLoc, currentTagData);
        if (bufCurLoc == NULL){
            return Error("Annotation Section Missing from list\n", -16);
        }
        strcpy(plist->video_list[i], currentTagData);
        bufCurLoc = getTagData("<EntryPoint>", "</EntryPoint>", bufCurLoc, currentTagData);
        if (bufCurLoc == NULL){
            return Error("EntryPoint Section Missing from list\n", -16);
        }
        plist->entry_points[i] = atoi(currentTagData);
        bufCurLoc = getTagData("<SourceDuration>", "</SourceDuration>", bufCurLoc, currentTagData);
        if (bufCurLoc == NULL){
            return Error("SourceDuration Section Missing from list\n", -16);
        }
        plist->durations[i] = atoi(currentTagData);
    }
    return 0;
}


int
dashVideoSegmentInitializer(int outsz, char *outbuf, FormatTriplet* pFormat, char* pLang, const char* szCPL, char *qlibid, char *qhash)
{

    const char* inputArray[] = { "-y" ,"-ss", "-i", "-to", "-map", "-vf", "-vcodec","-copyts","-b:v", "-x264opts", "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name", "-media_seg_name", "%s/dummy-v-0.XXXXXXXX"};
    const char* inputArrayVal[] = { "", "00:00:00", "%s/%s", "00:00:06", "0:v", "trim=start_frame=0:end_frame=144 ", "libx264", "", "%s", "keyint=144:min-keyint=144:scenecut=-1", "dash","6000000", "0", "0", "0", "%s-%sx%s-%s-init.XXXXXXXX", "dummy-0.XXXXXXXX", ""};
    const char* pCurLoc = szCPL;
    char* pMainImgToken = "<MainImageSequence>";
    char* pMainImageSeq = strstr(szCPL, pMainImgToken);
    if (pMainImageSeq == NULL){
        return Error("Could not find <MainImageSequence>\n",-1);
    }
    pCurLoc = pMainImageSeq + strlen(pMainImgToken);
    char* pAnnotationToken = "<Annotation>";
    char* pAnnotation = strstr(pCurLoc, pAnnotationToken);
    if (pAnnotation == NULL){
        return Error("Could not find <Annotation>\n", -2);
    }
    pCurLoc = pAnnotation + strlen(pAnnotationToken);
    char* pAnnotationEndToken = "</Annotation>";
    char* pAnnotationEnd = strstr(pCurLoc, pAnnotationEndToken);
    if (pAnnotationEnd == NULL) {
        return Error("Could not find </Annotation>\n", -3);
    }
    char szFilename[512];

    strncpy(szFilename, pCurLoc, pAnnotationEnd - pCurLoc);
    szFilename[pAnnotationEnd - pCurLoc] = 0;
    char keyArray[8192];
    char valArray[8192];

    char dummyFilename[256];
    char bandwidthValue[256];
    char resultFilename[512];
    char newAIValTargetFile[512];
    char dummyValue[512];

    sprintf(dummyFilename, inputArrayVal[2] , rootDir, szFilename);
    inputArrayVal[2] = dummyFilename;
    sprintf(bandwidthValue, inputArrayVal[8], pFormat->Bandwidth);
    inputArrayVal[8] = bandwidthValue;
    sprintf(resultFilename, inputArrayVal[15], pLang, pFormat->Width, pFormat->Height, pFormat->Bandwidth);
    inputArrayVal[15] = mktemp(resultFilename);
    sprintf(newAIValTargetFile, inputArray[17], rootDir);
    inputArray[17] = mktemp(newAIValTargetFile);
    sprintf(dummyValue, "%s", inputArrayVal[16]);
    inputArrayVal[16] = mktemp(dummyValue);

    printf("ffmpeg input file path=%s filename=%s\n", dummyFilename, szFilename);

    // Prepare input - read qpart and write out as the input file
    int res = resourceToFile(qlibid, qhash, szFilename, rootDir);
    if (res < 0) {
        return Error("Preparing ffmpeg input failed", res);
    }

    int bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
    int bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));
    res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);

    if (CLEANUP_FILES) {
        // Remove temporary input file
        if (unlink(dummyFilename) < 0) {
            printf("Failed to remove temporary ffmpeg input file %s\n", dummyFilename);
        }
        if (unlink(newAIValTargetFile) < 0){
            printf("Failed to remove temporary ffmpeg output file %s\n", newAIValTargetFile);
        }
    }

    if (res != 0){
        return Error("FFPEGRun Failed", res);
    }

    return loadFromFileAndPackData(outsz, outbuf,resultFilename, "video/mp4", 0);
}

int
dashVideoSegment(int outsz, char *outbuf, const char *argbuf, FormatTriplet* pFormat, char* pLang, char* pTrack, int els, const char* szCPL, char *qlibid, char *qhash)
{

    const char* inputArray[] = { "-y" , "-ss", "-to", "-i", "-map", "-vcodec", "-b:v", "-x264opts", "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name", "-media_seg_name", "%s/dummy-v.XXXXXXXX"};
    const char* inputArrayVal[] = { "",  "%f", "%f", "%s/%s",  "0:v", "libx264", "%s", "keyint=144:min-keyint=144:scenecut=-1", "dash","6006000.0", "0", "0", "0", "dummy.XXXXXXXX", "%s-%sx%s-%s-%s.XXXXXXXX", ""};
    const char* inputArrayComplex[] = { "-y", "-ss", "-to", "-i", "-ss", "-to", "-i", "-filter_complex",  "-map", "-vcodec", "-b:v", "-x264opts", "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name", "-media_seg_name", "%s/dummy-v.XXXXXXXX"};
    const char* inputArrayValComplex[] = { "", "%f", "%f", "%s/%s", "%f", "%f", "%s/%s",  "[0][1]concat=n=2[v]", "[v]", "libx264", "%s", "keyint=144:min-keyint=144:scenecut=-1", "dash","6006000.0", "0", "0", "0", "dummy.XXXXXXXX", "%s-%sx%s-%s-%s.XXXXXXXX", ""};
    char keyArray[8192];
    char valArray[8192];
    char dummyFilename[256];
    char bandwidthValue[256];
    char resultFilename[512];
    char newAIValTargetFile[512];
    char newTimeStart[512];
    char newTimeEnd[512];
    char newSecondTimeStart[512];
    char newSecondTimeEnd[512];
    char newSecondFile[512];
    char dummyValue[512];
    int iTrack = atoi(pTrack);
    int timeFrameStart = (iTrack - 1) * 144;
    int timeFrameEnd = iTrack * 144;
    const float framesize = 1001.0/24000.0;

    ComplexPlaylist playlist;
    playlist.durations = alloca(els*sizeof(playlist.durations));
    playlist.entry_points = alloca(els*sizeof(playlist.entry_points));
    playlist.video_list = alloca(els*sizeof(playlist.video_list));

    for (int i = 0; i < els; i++){
        playlist.video_list[i] = alloca(512);
    }

    int res = fullParseCPL(&playlist, szCPL);
    if (res != 0){
        return res;
    }


    int prevVal = 0;
    RealPlaylist* realPlaylist = alloca(sizeof(RealPlaylist) * playlist.element_count);
    prevVal = 0;
    for(int i=0; i<playlist.element_count;i++){
        realPlaylist[i].start = prevVal;
        realPlaylist[i].end = playlist.durations[i] + prevVal;
        realPlaylist[i].filename = playlist.video_list[i];
        prevVal = realPlaylist[i].end;
    }

    // framestart and end in a single file and which one
    // else we need 2 with boundries
    int playlistStartIndex=0;
    int playlistEndIndex=0;
    for (int i =0; i< playlist.element_count;i++){
        if (realPlaylist[i].start <= timeFrameStart && realPlaylist[i].end > timeFrameStart){
            playlistStartIndex = i;
        }
        if (realPlaylist[i].start <= timeFrameEnd && realPlaylist[i].end > timeFrameEnd){
            playlistEndIndex = i;
        }
    }
    //printf ("Track start index = %d, end index = %d\n", playlistStartIndex, playlistEndIndex);
    //printf ("Track %d,  start = %f, end = %f filename = %s\n", playlistStartIndex, realPlaylist[playlistStartIndex].start, realPlaylist[playlistStartIndex].end, realPlaylist[playlistStartIndex].filename );
    //printf ("Track %d,  start = %f, end = %f filename = %s\n", playlistEndIndex, realPlaylist[playlistEndIndex].start, realPlaylist[playlistEndIndex].end, realPlaylist[playlistEndIndex].filename);

    RealPlaylist adjusted[2];
    int isAdjusted = 0;
    if (playlistStartIndex == playlistEndIndex){
        float clipstartOffset = timeFrameStart-realPlaylist[playlistStartIndex].start;
        adjusted[0].start = (clipstartOffset+playlist.entry_points[playlistStartIndex])*framesize;
        adjusted[0].end = (clipstartOffset+144+playlist.entry_points[playlistStartIndex])*framesize;
        adjusted[0].filename = realPlaylist[playlistStartIndex].filename;
    }
    else{
        float clipstartOffset = timeFrameStart-realPlaylist[playlistStartIndex].start;
        int resourceDuration =  playlist.durations[playlistStartIndex];
        int resourceFramesToUse = resourceDuration - clipstartOffset;
        adjusted[0].start =  clipstartOffset*framesize;
        adjusted[0].end = (clipstartOffset+resourceFramesToUse)*framesize;
        adjusted[0].filename = realPlaylist[playlistStartIndex].filename;
        adjusted[1].start = playlist.entry_points[playlistEndIndex]*framesize;
        int resourceFramesToUse2 = 144 - resourceFramesToUse;
        adjusted[1].end = (playlist.entry_points[playlistEndIndex] + resourceFramesToUse2)*framesize;
        adjusted[1].filename = realPlaylist[playlistEndIndex].filename;
        isAdjusted = 1;
    }

    if (isAdjusted == 1){
        sprintf(newTimeStart, inputArrayValComplex[1], adjusted[0].start);
        inputArrayValComplex[1] = newTimeStart;
        sprintf(newTimeEnd, inputArrayValComplex[2], adjusted[0].end);
        inputArrayValComplex[2] = newTimeEnd;
        sprintf(dummyFilename, inputArrayValComplex[3] , rootDir, adjusted[0].filename);
        inputArrayValComplex[3] = dummyFilename;
        sprintf(newSecondTimeStart, inputArrayValComplex[4], adjusted[1].start);
        inputArrayValComplex[4] = newSecondTimeStart;
        sprintf(newSecondTimeEnd, inputArrayValComplex[5], adjusted[1].end);
        inputArrayValComplex[5] = newSecondTimeEnd;
        sprintf(newSecondFile, inputArrayValComplex[6] , rootDir, adjusted[1].filename);
        inputArrayValComplex[6] = newSecondFile;
        sprintf(bandwidthValue, inputArrayValComplex[10], pFormat->Bandwidth);
        inputArrayValComplex[10] = bandwidthValue;
        sprintf(resultFilename, inputArrayValComplex[18], pLang, pFormat->Width, pFormat->Height, pFormat->Bandwidth, pTrack);
        inputArrayValComplex[18] = mktemp(resultFilename);
        sprintf(newAIValTargetFile, inputArrayComplex[19], rootDir);
        inputArrayComplex[19] = mktemp(newAIValTargetFile);
        sprintf(dummyValue, "%s", inputArrayValComplex[17]);
        inputArrayValComplex[17] = mktemp(dummyValue);


        // Prepare input - read qpart and write out as the input file
        printf("ffmpeg input files %s, %s\n",
            adjusted[0].filename, adjusted[1].filename);
        if ((res = resourceToFile(qlibid, qhash, adjusted[0].filename, rootDir)) < 0 ||
            (res = resourceToFile(qlibid, qhash, adjusted[1].filename, rootDir)) < 0) {
            return Error("Preparing ffmpeg input failed", res);
        }

        int bufLen = packStringArray(keyArray, inputArrayComplex,  sizeof(inputArrayComplex)/sizeof(const char*));
        int bufValLen = packStringArray(valArray, inputArrayValComplex, sizeof(inputArrayValComplex)/sizeof(const char*));
        res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);

    }
    else{
        sprintf(newTimeStart, inputArrayVal[1], adjusted[0].start);
        inputArrayVal[1] = newTimeStart;
        sprintf(newTimeEnd, inputArrayVal[2], adjusted[0].end);
        inputArrayVal[2] = newTimeEnd;
        sprintf(dummyFilename, inputArrayVal[3] , rootDir, adjusted[0].filename);
        inputArrayVal[3] = dummyFilename;
        sprintf(bandwidthValue, inputArrayVal[6], pFormat->Bandwidth);
        inputArrayVal[6] = bandwidthValue;
        sprintf(resultFilename, inputArrayVal[14], pLang, pFormat->Width, pFormat->Height, pFormat->Bandwidth, pTrack);
        inputArrayVal[14] = mktemp(resultFilename);

        sprintf(newAIValTargetFile, inputArray[15], rootDir);
        inputArray[15] = mktemp(newAIValTargetFile);
        sprintf(dummyValue, "%s", inputArrayVal[13]);
        inputArrayVal[13] = mktemp(dummyValue);

        // Prepare input - read qpart and write out as the input file
        printf("ffmpeg input file %s\n",
            adjusted[0].filename);
        if ((res = resourceToFile(qlibid, qhash, adjusted[0].filename, rootDir)) < 0) {
            return Error("Preparing ffmpeg input failed", res);
        }

        int bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
        int bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));
        res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);
    }

    if (CLEANUP_FILES) {
        // Remove temporary input file
        if (unlink(dummyFilename) < 0) {
            printf("Failed to remove temporary ffmpeg dummyfile %s\n", dummyFilename);
        }
        if (isAdjusted && (unlink(newSecondFile) < 0)) {
            printf("Failed to remove temporary ffmpeg dummyfile %s\n", newSecondFile);
        }
        if (unlink(newAIValTargetFile) < 0){
            printf("Failed to remove temporary ffmpeg output file %s\n", newAIValTargetFile);
        }
    }
    if (res == 0){
        res =  loadFromFileAndPackData(outsz, outbuf,resultFilename, "video/mp4", iTrack);

    }

    return res;
}

int
dashAudioSegmentInitializer(int outsz, char *outbuf, char* audiotype, char* bandwidth, char* pLang, char* qlibid, char* qhash)
{
    const char* inputArray[] = { "-y" ,"-ss", "-i", "-to", "-map", "-acodec", "-b:a",  "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name", "-media_seg_name", "%s/dummy-a0.XXXXXXXX"};
    const char* inputArrayVal[] = { "", "00:00:00", "%s/%s", "00:00:06", "0:a",  "aac", "%s",  "dash","6000000", "0", "0", "0", "%s-%s-%s-init.XXXXXXXX", "dummy-0.XXXXXXXX", ""};

    char* szCPL = getCPLByName(qlibid, qhash, pLang, outbuf); //use outbuf until we know how big
    char* pCurLoc = alloca(strlen(szCPL)+1);
    strcpy(pCurLoc, szCPL);
    char* pMainImgToken = "<MainAudioSequence>";
    char* pMainImageSeq = strstr(szCPL, pMainImgToken);
    if (pMainImageSeq == NULL){
        return Error("Could not find <MainAudioSequence>\n",-1);
    }
    pCurLoc = pMainImageSeq + strlen(pMainImgToken);
    char* pAnnotationToken = "<Annotation>";
    char* pAnnotation = strstr(pCurLoc, pAnnotationToken);
    if (pAnnotation == NULL){
        return Error("Could not find <Annotation>\n",-2);
    }
    pCurLoc = pAnnotation + strlen(pAnnotationToken);
    char* pAnnotationEndToken = "</Annotation>";
    char* pAnnotationEnd = strstr(pCurLoc, pAnnotationEndToken);
    if (pAnnotationEnd == NULL) {
        return Error("Could not find </Annotation>\n",-3);
    }
    char szFilename[512];

    strncpy(szFilename, pCurLoc, pAnnotationEnd - pCurLoc);
    szFilename[pAnnotationEnd - pCurLoc] = 0;
    char keyArray[8192];
    char valArray[8192];

    char dummyFilename[256];
    char bandwidthValue[256];
    char resultFilename[512];
    char newAIValTargetFile[512];
    char dummyValue[512];

    sprintf(dummyFilename, inputArrayVal[2] , rootDir, szFilename);
    inputArrayVal[2] = dummyFilename;
    sprintf(bandwidthValue, inputArrayVal[6], bandwidth);
    inputArrayVal[6] = bandwidthValue;
    sprintf(resultFilename, inputArrayVal[12], pLang, audiotype, bandwidth);
    inputArrayVal[12] = mktemp(resultFilename);
    sprintf(newAIValTargetFile, inputArray[14], rootDir);
    inputArray[14] = mktemp(newAIValTargetFile);
    sprintf(dummyValue, "%s", inputArrayVal[13]);
    inputArrayVal[13] = mktemp(dummyValue);

    printf("ffmpeg input file path=%s filename=%s\n", dummyFilename, szFilename);

    // Prepare input - read qpart and write out as the input file
    int res = resourceToFile(qlibid, qhash, szFilename, rootDir);
    if (res < 0) {
        return Error("Preparing ffmpeg input failed", res);
    }

    int bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
    int bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));

    res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);

    if (CLEANUP_FILES) {
        // Remove temporary input file
        if (unlink(dummyFilename) < 0) {
            printf("Failed to remove temporary ffmpeg input file %s\n", dummyFilename);
        }
        if (unlink(newAIValTargetFile) < 0){
            printf("Failed to remove temporary ffmpeg output file %s\n", newAIValTargetFile);
        }
    }
    if (res != 0){
        return Error("FFPEGRun Failed", res);
    }

    return loadFromFileAndPackData(outsz, outbuf,resultFilename, "audio/mp4", 0);

}

int
dashAudioSegment(int outsz, char *outbuf, char* audiotype, char* bandwidth, char* pLang, char* pTrack, char* qlibid, char* qhash)
{
    const char* inputArray[] = { "-y" , "-ss", "-to", "-i", "-map", "-frames:a", "-acodec", "-b:a", "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name", "-media_seg_name", "%s/dummy-a.XXXXXXXX"};
    const char* inputArrayVal[] = { "", "%f",   "%f", "%s/%s", "0:a", "282", "aac", "%s",  "dash","7000000", "0", "0", "0", "dummy-0.XXXXXXXX", "%s-%s-%s-%s.XXXXXXXX", ""};

    char* szCPL = getCPLByName(qlibid, qhash, pLang, outbuf); // use outbuf as it has sufficient storage until we know its size

    char* pCurLoc = alloca(strlen(szCPL)+1);
    strcpy(pCurLoc, outbuf);
    char* pMainImgToken = "<MainAudioSequence>";
    char* pMainImageSeq = strstr(szCPL, pMainImgToken);
    if (pMainImageSeq == NULL){
        return Error("Could not find <MainAudioSequence>\n",-1);
    }
    pCurLoc = pMainImageSeq + strlen(pMainImgToken);
    char* pAnnotationToken = "<Annotation>";
    char* pAnnotation = strstr(pCurLoc, pAnnotationToken);
    if (pAnnotation == NULL){
        return Error("Could not find <Annotation>\n", -2);
    }
    pCurLoc = pAnnotation + strlen(pAnnotationToken);
    char* pAnnotationEndToken = "</Annotation>";
    char* pAnnotationEnd = strstr(pCurLoc, pAnnotationEndToken);
    if (pAnnotationEnd == NULL) {
        return Error("Could not find </Annotation>\n", -3);
    }
    char szFilename[512];

    strncpy(szFilename, pCurLoc, pAnnotationEnd - pCurLoc);
    szFilename[pAnnotationEnd - pCurLoc] = 0;
    char keyArray[8192];
    char valArray[8192];

    char dummyFilename[256];
    char bandwidthValue[256];
    char resultFilename[512];
    char newAIValTargetFile[512];
    char newTimeTo[256];
    char newSS[256];
    char dummyValue[512];

    int iTrack = atoi(pTrack);
    float timeTo = iTrack*6.016;
    float timeStart = (iTrack-1)*6.016;

    sprintf(newSS, inputArrayVal[1], timeStart);
    inputArrayVal[1] = newSS;
    sprintf(newTimeTo, inputArrayVal[2], timeTo);
    inputArrayVal[2] = newTimeTo;
    sprintf(dummyFilename, inputArrayVal[3] , rootDir, szFilename);
    inputArrayVal[3] = dummyFilename;

    sprintf(bandwidthValue, inputArrayVal[7], bandwidth);
    inputArrayVal[7] = bandwidthValue;
    sprintf(resultFilename, inputArrayVal[14], pLang, audiotype, bandwidth, pTrack);
    inputArrayVal[14] = mktemp(resultFilename);
    sprintf(newAIValTargetFile, inputArray[15], rootDir);
    inputArray[15] = mktemp(newAIValTargetFile);
    sprintf(dummyValue, "%s", inputArrayVal[13]);
    inputArrayVal[13] = mktemp(dummyValue);

    // Prepare input - read qpart and write out as the input file
    int res = resourceToFile(qlibid, qhash, szFilename, rootDir);
    if (res < 0) {
        return Error("Preparing ffmpeg input failed", res);
    }

    int bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
    int bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));

    res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);

    if (CLEANUP_FILES) {
        // Remove temporary input file
        if (unlink(dummyFilename) < 0) {
            printf("Failed to remove temporary ffmpeg input file %s\n", dummyFilename);
        }
        if (unlink(newAIValTargetFile) < 0){
            printf("Failed to remove temporary ffmpeg output file %s\n", newAIValTargetFile);
        }
    }
    if (res != 0){
        return Error("FFPEGRun Failed", res);
    }

    return loadFromFileAndPackData(outsz, outbuf,resultFilename, "audio/mp4", iTrack);
}

/*
 * Test write operations
 *
 * Arguments:
 *   qlibid
 *   qwt
 *   big buffer (10MB)
 */
int
test(int outsz, char *outbuf, const char *argbuf)
{
    int rc = 0;
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;

    printf("test start\n");

    argc = buf2argv(argbuf, &argv, &argsz);
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i ++) {
        printf("arg%d (%d) = %.16s\n", i, argsz[i], argv[i]);
    }

    if (argc < 3)
        return -1;

    char *qlibid = argv[0];
    char *wrtoken = argv[1];

    rc = KVSet(qlibid, wrtoken, "SS TEST", "SS VALUE 1");
    printf("KVSet res=%d\n", rc);

    char *v = KVGetTemp(qlibid, wrtoken, "SS TEST");
    printf("KVGet rc=%d v=%s\n", rc, v ? v : "NULL");

    rc = KVPushBack(qlibid, wrtoken, "SS LIST 2", "SS VALUE 1");
    printf("KVPushBack res=%d\n", rc);
    rc = KVPushBack(qlibid, wrtoken, "SS LIST 2", "SS VALUE 2");
    printf("KVPushBack res=%d\n", rc);
    rc = KVPushBack(qlibid, wrtoken, "SS LIST 2", "SS VALUE 3");
    printf("KVPushBack res=%d\n", rc);


    char *elems = KVRangeTemp(qlibid, wrtoken, "SS LIST 2");
    if (elems == NULL) elems = strdup("");
    int nelems = 0;
    char **elemv = NULL;
    uint32_t *elemsz = NULL;
    nelems = buf2argv(elems, &elemv, &elemsz);
    for (int i = 0; i < nelems; i ++) {
        printf("KVRangeTemp result - [%d] = %s\n", i, elemv[i]);
    }
    freeargv(nelems, elemv, elemsz);

    /* Make a large output buffer */
    int soutsz = 40 * 1024 * 1024;
    char *sout = malloc(soutsz * sizeof(char));
    for (int i = 0; i < soutsz; i ++) {
        sout[i] = '3';
    }

    /* Prepare output */
    char *outargv[3];
    uint32_t outargsz[3];
    outargv[0] = "OUTPUT ONE";
    outargsz[0] = strlen(outargv[0]);
    outargv[1] = "OUTPUT TWO";
    outargsz[1] = strlen(outargv[1]);
    outargv[2] = sout;
    outargsz[2] = soutsz;
    argv2buf(3, (const char **)outargv, outargsz, outbuf, outsz);

    free(sout);
    return 0;
}


/*
 * Test read operations
 *
 * Arguments:
 *   qlibid
 *   qwt
 *   big buffer (10MB)
 */
int
rtest(int outsz, char *outbuf, const char *argbuf)
{
    int rc = 0;
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;

    printf("rtest start\n");

    argc = buf2argv(argbuf, &argv, &argsz);
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i ++) {
        printf("arg%d (%d) = %.16s\n", i, argsz[i], argv[i]);
    }

    if (argc < 3)
        return -1;

    char *qlibid = argv[0];
    char *qhash = argv[1];
    char *phash = argv[2];

    char *v = KVGet(qlibid, qhash, "resource.VIDEO-001.name");
    printf("KVGet rc=%d v=%s\n", rc, v ? v : "NULL");

    char *elems = KVRange(qlibid, qhash, "resources");
    if (elems == NULL) elems = strdup("");
    int nelems = 0;
    char **elemv = NULL;
    uint32_t *elemsz = NULL;
    nelems = buf2argv(elems, &elemv, &elemsz);
    for (int i = 0; i < nelems; i ++) {
        printf("KVRange result - [%d] = %s\n", i, elemv[i]);
    }
    freeargv(nelems, elemv, elemsz);

    /* Read a part */
    uint32_t psz = 0;
    char *c = QReadPart(qlibid, qhash, phash, 0, 0, &psz);
    printf("QReadPart sz=%d contents=%.1024s\n", psz, c);

    /* Make a large output buffer */
    int soutsz = 40 * 1024 * 1024;
    char *sout = malloc(soutsz * sizeof(char));
    for (int i = 0; i < soutsz; i ++) {
        sout[i] = '3';
    }

    /* Prepare output */
    char *outargv[3];
    uint32_t outargsz[3];
    outargv[0] = "OUTPUT ONE";
    outargsz[0] = strlen(outargv[0]);
    outargv[1] = "OUTPUT TWO";
    outargsz[1] = strlen(outargv[1]);
    outargv[2] = sout;
    outargsz[2] = soutsz;
    argv2buf(3, (const char **)outargv, outargsz, outbuf, outsz);

    free(sout);

    free(v);
    free(elems);

    free(c);

    return 0;
}

/*
 * Dispatch content requests
 *
 * For DASH there are several types of requests:
 *   - manifest (.mpd)
 *   - video segment (.m4v)  (initializer segment)
 *   - audio segment (.m4a)  (initializer segment has no segno)
 *
 * Arguments:
 *   qlibid       - content library ID
 *   qhash        - content 'hash'
 *   URL path (e.g.: "/dash/EN-1280x544-1300000-init.m4v")
 *

Example URLs:
  Manifest: http://localhost:4567/dash/EN.mpd
  Video Init Segment: http://localhost:4567/dash/EN-1280x544-1300000-init.m4v
  Video Segment: http://localhost:4567/dash/EN-1280x544-1300000-1.m4v
  Audio Init Segment: http://localhost:4567/dash/EN-STERO-3200-init.m4a
  Audio Segment: http://localhost:4567/dash/EN-STEREO-3200-1.m4a

 */
int
content(int outsz, char *outbuf, const char *argbuf)
{
    typedef enum eMKTypes { VIDEO , AUDIO, MANIFEST, TEST, HTML } MKTypes;

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
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    char szMK[5];
    strcpy(szMK, (pContentRequest+cbContentRequest-3));
    int szUrl = 4*1024;
    char url[szUrl];
    /*FIXME: Need a way to get the url instead of localhost:8008 */
    snprintf(url, szUrl, "http://localhost:8008/qlibs/%s/q/%s/rep%s",qlibid,qhash,pContentRequest);

    char *dot = strrchr(pContentRequest, '.');
    if (!dot)
      return -1;

    if (strcmp(dot, ".mpd") == 0)  // really need to return error if not matching any
        mk = MANIFEST;
    else if (strcmp(dot, ".m4v") == 0)
        mk = VIDEO;
    else if (strcmp(dot, ".m4a") == 0)
        mk = AUDIO;
    else if (strcmp(dot, ".tst") == 0)
        mk = TEST;
    else if (strcmp(dot, ".html") == 0)
        mk = HTML;
    else
        return -1;

//   URL path (e.g.: "/dash/EN-1280x544-1300000-init.m4v")

    char* szPreamble = alloca(cbContentRequest);
    char szLanguage[10];
    char szWidthHeight[20];
    char* szWidth = szWidthHeight;
    char* szHeight;
    char bandwidth[10];
    char track[10];

    switch(mk){
        case AUDIO:
        {
            szLanguage[0] = 0;
            for (int i=0; i < cbContentRequest;i++){
                char curCh = pContentRequest[i];
                if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                    pContentRequest[i] = ' ';
            }
            sscanf(pContentRequest, "%s%s%s%s%s%s", szPreamble, szLanguage, szWidthHeight, bandwidth, track, szMK);

            printf("AUDIO TYPE = %s", szWidthHeight);

            if (strcmp(track, "init") == 0)
                //MM
                res = dashAudioSegmentInitializer(outsz,outbuf, szWidthHeight, bandwidth, szLanguage, qlibid, qhash);
            else
                //MM
                res = dashAudioSegment(outsz,outbuf, szWidthHeight, bandwidth, szLanguage, track, qlibid, qhash);


            break;
        }
        case VIDEO:
        {
            szLanguage[0] = 0;
            for (int i=0; i < cbContentRequest;i++){
                char curCh = pContentRequest[i];
                if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                    pContentRequest[i] = ' ';
            }
            sscanf(pContentRequest, "%s%s%s%s%s%s", szPreamble, szLanguage, szWidthHeight, bandwidth, track, szMK);

            char* ptemp = getCPLByName(qlibid, qhash, szLanguage,outbuf); // use outbuf as it has sufficient storage until we know its size
            char* szCPL = alloca(strlen(ptemp)+1);
            strcpy(szCPL, ptemp);
            int element_count = calculateResourceCount(szCPL);
            int cbWidthHeight = strlen(szWidthHeight);

            for (int i=0;i<cbWidthHeight;i++){
                if (szWidthHeight[i] == 'x'){
                    szWidthHeight[i] = 0;
                    szHeight = &szWidthHeight[i+1];
                }
            }
            printf("%s,%s,%s,%s,%s,%s\n", szPreamble, szLanguage, szWidth, szHeight, bandwidth, track);
            FormatTriplet format;
            format.Height = szHeight;
            format.Width = szWidth;
            format.Bandwidth = bandwidth;

            if (strcmp(track, "init") == 0){
                // MM
                res = dashVideoSegmentInitializer(outsz,outbuf,&format, szLanguage, szCPL, qlibid, qhash);
            }
            else{
                res = dashVideoSegment(outsz,outbuf,argbuf, &format, szLanguage, track, element_count, szCPL, qlibid, qhash);
            }
        }
        break;
        case MANIFEST:
            {
                *(pContentRequest+cbContentRequest-4) = 0;
                char* pLang = pContentRequest+cbContentRequest-6;

                // Look up video formats in the KV store

                // Prepare args
                char argbuf[1024];
                char *argv[2];
                uint32_t argvsz[2];
                argv[0] = qlibid;
                argvsz[0] = strlen(qlibid);
                argv[1] = qhash;
                argvsz[1] = strlen(qhash);
                argv2buf(2, (const char **)argv, argsz, argbuf, sizeof(argbuf));

                char foutbuf[1024];
                int rc = formatsGet(sizeof(foutbuf), foutbuf, argbuf);
                if (rc < 0) {
                    printf("Failed formatsGet rc=%d\n", rc);
                    // FIXME cleanup and return
                }

                char **fargv;
                uint32_t *fargsz;
                int foutsz = buf2argv(foutbuf, &fargv, &fargsz);

                // iterate over output buf to create the triplet
                FormatTriplet *video_formats = (FormatTriplet *)(calloc(foutsz / 3, sizeof(FormatTriplet)));
                for (int i = 0; i < foutsz / 3; i ++) {
                    video_formats[i].Height = fargv[3 * i];
                    video_formats[i].Width = fargv[3 * i + 1];
                    video_formats[i].Bandwidth = fargv[3 * i + 2];
                }

#if 1
                {
                    // Create special file indicating 'access requuest'
                    FILE *f = fopen("xcrypt-accessrequest", "w");
                    printf("xcrypt accessrequest f=%p %d\n", f, errno);
                    int rc = fprintf(f, "%s %s\n", qlibid, qhash);
                    printf("xcrypt accessrequest write rc=%d\n", rc);
                    fclose(f);
                }
#endif

                char buf[100 * 1024];
                char *bufptr = dashManifest(sizeof(buf), buf, video_formats, foutsz / 3, pLang);
                if (bufptr == NULL) {
                    // FIXME clean up and return
                }
                /* Prepare output */
                char *outargv[2];
                uint32_t outargsz[2];
                outargv[0] = "application/xml"; /* c */
                outargsz[0] = strlen(outargv[0]);
                outargv[1] = buf;
                outargsz[1] = strlen(buf);
                argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);
            }
            break;
    case TEST:
        dashTest(outsz, outbuf, pContentRequest);
        break;
    case HTML:
        dashHtml(outsz, outbuf, url);
        break;
    default:
        res = -1;
        break;
    };
    freeargv(argc, argv, argsz);
    return res;
}
