
#ifndef __WASMCEPTION_H
#define __WASMCEPTION_H

#define WASM_EXPORT __attribute__ ((visibility ("default")))

#endif // __WASMCEPTION_H




// * LIVE_RTP - Live real time


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#ifdef __linux__
#define __USE_XOPEN_EXTENDED
#endif
#include <ftw.h>


#include "qspeclib.h"

#include "../avmaster/fixup.h"


/* Core and Extended API */
extern int64_t FFMPEGRun(char*, int, char*, int);
extern int FFMPEGRunLive(char*, int, char*, int, char*,char*,char*,char*);
extern void FFMPEGStopLive(int);
extern void FFMPEGTest();

extern void LOGMsg(char *);
extern int KVSet(char *, char *, char *, char *);
extern char *KVGetTemp(char *, char *, char *);
extern int KVPushBack(char *, char *, char *, char *);
extern char *KVRangeTemp(char *, char *, char *);
extern char *QCreatePart(char *, char *, char *, uint32_t);
extern char *QReadPart(char *, char *, char *, uint32_t, uint32_t, uint32_t *);
extern char *QReadPartTemp(char *, char *, char *, uint32_t, uint32_t, uint32_t *);
extern char *KVGet(char *, char *, char *);
extern char *KVRange(char *, char *, char *);

// Global strings
const char* segment_storer = "segment.storer";
const char* segment_init = "segment.init";
const char* stream_handle = "stream.handle";

typedef enum eMKTypes { VIDEO , AUDIO, MANIFEST, TEST } MKTypes;

typedef struct tagFormatTriplet {
    char* Height;
    char* Width;
    char* Bandwidth;
} FormatTriplet;

const int CLEANUP_FILES=1;

/*
 * Arguments:
 *   qlibid string - the content library id
 *   wrtoken string - the content writetoken
 *   formats array - the w1,h1,bw1 w2,h2,bw2, ... wn,hn,bwn
 * Return:
 *   none
 */
int formatsSet(int outsz, char *outbuf, const char *argbuf)
{
    printf("formatsSet\n");
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
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 * Return:
 *   none
 *
 *  Stops an FFMPegProcess by finding its Pid in the segment storer
 *  and then finds its stream handle calling go routine to stop
 */
int stop_stream(int outsz, char *outbuf, const char *argbuf)
{
    int argc;
    char **argv;
    uint32_t *argsz;

    argc = buf2argv(argbuf, &argv, &argsz);
    char* qlibid = argv[0];
    char* qhash = argv[1];

    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Failed to find segment storer\n");
        return -1;
    }
    char* shandle = KVGetTemp ( qlibid, ssqwt, (char*)stream_handle);
    if (shandle == NULL) {
        printf("Failed to find stream handle\n");
        return -1;
    }

    // Must remove process handle temp key

    FFMPEGStopLive(atoi(shandle));
    return 0;
}


int Error(char* message, int code){
    printf("%s\n", message);
    return code;
}

/*
 * Arguments:
 *  inputBuffer: destination for count prefixed array of length prefixed strings
 *  arrayToPack: array of strings (char**)
 *  stringCount: count of strings (0 terminated)
 * Return:
 *   lenght of resultant packed buffer
 *
 *  Packs a continguous bytes stream with an an array of N strings
 *  where N is the prefix of the buffer and each string is length prefixed
 */
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

/*
 * Arguments:
 *  filename: file to load data from
 *  psz: out buffer size 0 on failure
 * Return:
 *   pointer to malloc'd buffer NULL on failure
 *   *************************
 *   CALLER MUST FREE!!!!!!!
 *
 *  Loads all data from file and returns a malloc'd buffer with the resultant
 *  byte stream. bytes read are placed in output param
 *
 */
char* LoadDataFromFile(char* filename, int* psz){
    FILE* targetFile = fopen(filename, "rb");
    *psz = 0;

    if (!targetFile){
        printf("Error: Unable to open %s\n", filename);
        return 0;
    }
    fseek(targetFile, 0, SEEK_END);
    long fsize = ftell(targetFile);
    fseek(targetFile, 0, SEEK_SET);  //same as rewind(f);
    char *segData = 0;

    if (fsize > 0){
        const int allocSize = fsize + 1;
        segData = malloc(allocSize);
        segData[fsize] = 0;
        int cb = fread(segData, 1, fsize, targetFile);
        if (cb != fsize){
            free(segData);
            printf("Error : Did not read all the data\n");
            segData = 0;
        }
        else{
            printf("Data read size = %ld into segData\n", fsize);
        }
    }

    fclose(targetFile);

    if (CLEANUP_FILES){
        if (unlink(filename) < 0) {
            printf("Failed to remove temporary ffmpeg output file %s\n", filename);
        }
    }
    if (segData == 0){
        printf("ERROR!!! LoadDataFromFile failed on file %s\n", filename);
        return 0;
    }
    *psz = fsize;
    return segData; // this must be free'd by caller
}

/*
 *  See nwlink callback ref
 */
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath); //just nuke it

    if (rv)
        perror(fpath);

    return rv;
}

/*
 * Arguments:
 *  path: path to directory to remove
 * Return:
 *   success or error value
 *
 *  removes the directory and all its contents
 *
 */
int removeTempDir(char *path)
{
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

/*
 * Arguments:
 *  template: string of format "temp/tempXXXXXXXX"
 * Return:
 *   pointer to the template input parameter
 *
 *  creates a unique temporary directory at the CWD
 *
 */
char* createTempDir(char* template){

    char origTemplate[1024];
    int done =0;
    char* tmpDirName = 0;
    strcpy(origTemplate, template);
    while(!done){
        strcpy(template, origTemplate);
        tmpDirName = mktemp(template);

        struct stat st = {0};

        if (stat(tmpDirName, &st) == -1) {
            mkdir(tmpDirName, 0700);
            done = 1;
        }
    }
    return tmpDirName;
}


/*
 * Arguments:
 *  name: string of format live-(1/0)-timestamp.m4s
 *  timestamp: location for timestamp
 * Return:
 *   pointer to timestamp parameter
 *
 *  extracts the timestamp form a filename of format live-(1/0)-timestamp.m4s
 *  timestampt parameter receives the output,
 *
 */
char* extractTimestamp(char* name, char* timestamp){
    const char* mediaTag = "live-";
    const char* initTag = "-init";
    const char* initTimestamp = "init";

    if (strstr(name, initTag))
        strcpy(timestamp, initTimestamp);
    else{
        char* media = strstr(name, mediaTag);
        if (media == NULL){
            timestamp = NULL;
            return NULL;
        }
        char* beginTimestamp = media + strlen(mediaTag) + strlen("0-");
        char* curLoc = beginTimestamp;
        while(curLoc != NULL && *curLoc != '.')
            curLoc++;
        strncpy(timestamp, beginTimestamp, curLoc-beginTimestamp);
        timestamp[curLoc-beginTimestamp]=0;
    }
    return timestamp;
}

/* Write out a 'resource' as a file for ffmpeg to use as input */
static int
bufferToFile(char *buffer, char *resname, char *outdir, int size)
{
    /* Write the part to file */
    char *fname = (char *)alloca(strlen(outdir) + 1 + strlen(resname));
    sprintf(fname, "%s/%s", outdir, resname);
    int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Failed to open temp file\n");
        return -1;
    }
    int rc = write(fd, buffer, size);
    if (rc < size) {
        printf("Failed to write temp file\n");
        return -1;
    }
    close(fd);
    return 0;
}

/*
 * Arguments:
 *  outsz: size of outbuf
 *  outbuf: pointer to memory of size outsz
 *  filename: file to load data from
 *  origFilename : source of filename (concatenated file of init and sect raw)
 *  contenttype : "video/mp4" or "audio/mp4"
 *  rootDir : root folder to look
 * Return:
 *   0 on success all else failure
 *
 *  Loads the new file and original and calls ffmpegFixupLive on the newfile
 *  packs the resultant bits in outbuf using standard argv2buf
 */
int
loadFromFileAndPackData(int outsz, char* outbuf, char* filename, char* origFilename, char* contenttype, char* rootDir){
    int res = 0;
    char szTarget[1024];
    sprintf(szTarget, "%s/%s", rootDir, filename);
    FILE* pTargetFile = fopen(szTarget, "rb");

    if (!pTargetFile){
        return Error("Error: Unable to open", -4);
    }
    fseek(pTargetFile, 0, SEEK_END);
    long fsize = ftell(pTargetFile);
    fseek(pTargetFile, 0, SEEK_SET);  //same as rewind(f);

    char *segData = malloc(fsize + 1);
    int cb = fread(segData, 1, fsize, pTargetFile);
    if (cb != fsize){
        free(segData);
        fclose(pTargetFile);
        return Error("Did not read all the data", -6);
    }
    fclose(pTargetFile);

    sprintf(szTarget, "%s/%s", rootDir, origFilename);
    pTargetFile = fopen(szTarget, "rb");

    if (!pTargetFile){
        return Error("Error: Unable to open", -4);
    }
    fseek(pTargetFile, 0, SEEK_END);
    long fsizeOrig = ftell(pTargetFile);
    fseek(pTargetFile, 0, SEEK_SET);  //same as rewind(f);

    char *segDataOrig = malloc(fsizeOrig + 1);
    cb = fread(segDataOrig, 1, fsizeOrig, pTargetFile);
    if (cb != fsizeOrig){
        free(segData);
        free(segDataOrig);
        fclose(pTargetFile);
        return Error("Did not read all the data", -6);
    }
    fclose(pTargetFile);

    if (strstr(filename, "-init") == 0){
        printf("CALLING FIXUP NOW!!!!!!!!!!\n");
        /* Fix ffmpeg segment sequence and base time in place */
        res = ffmpegFixupLive(segData, fsize, segDataOrig, fsizeOrig);
        if (res < 0) {
            free(segData);
            free(segDataOrig);
            return Error("Failed to fix segment", -8);
        }
    }

    /* Prepare output */
    char *outargv[2];
    uint32_t outargsz[2];
    outargv[0] = contenttype;
    outargsz[0] = strlen(outargv[0]);
    outargv[1] = segData;
    outargsz[1] = fsize;
    argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);
    free(segData);
    free(segDataOrig);
    return res;

}

/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   outbufsize - output size of buffer
 *   outbuf - buffer of size (outbufsize)
 *   format FormatTriplet : width height and badwidth
 *   segName - string requested segment (timestamp or init)
 * Return:
 *   0 on success failure otherwise
 *
 *  delivers segment bytes to outbuf
 *  handles all segments including init
 *
 */
int dashSegment(char* qlibid, char* qhash, int outbufsize, char *outbuf, FormatTriplet* format, char* segName){
    const char* inputArray[] = { "-hide_banner","-nostats", "-loglevel", "-y", "-i"   , "-copyts", "-map", "-vcodec","-b:v", "-vf"         ,"-x264opts"                           , "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name"      , "-media_seg_name", "%s/dummy.mpd"};
    const char* inputArrayVal[] = { "",         "",         "error"    ,""   , "%s/%s", ""        ,"0:v" ,"libx264" , "%s" , "scale=%s:%s ", "keyint=60:min-keyint=60:scenecut=-1", "dash","2000000.0"       , "1"             , "1"           , "0"              , "dummy-0.m4v"       , "%sx%s-%s-%s.m4v"  , ""};

    const char* dummyFile = "dummy-0.m4v";
    const char* initTemplate = "%sx%s-%s-init.m4v";
    const char* segTemplate = "%sx%s-%s-%s.m4v";
    char segmentName[256];


    strcpy(segmentName, segName);

    int isInit = strcmp(segmentName, "init") == 0;
    int isVideo = (format->Height != 0 && format->Width != 0);
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Error: could not find segment storer\n");
        return -1;
    }
    const int keysz = 1024;
    char keybuf[keysz];
    char segment_init[128];

    sprintf(segment_init, "segment.%s.init", isVideo ? "video" : "audio");
    char* parthash = KVGetTemp(qlibid, ssqwt, segment_init);
    if (parthash == 0){
        printf("Error: could not find %s \n", segment_init);
        return -17;
    }
    uint32_t size=0, sizeFirst=0;
    char* ret = QReadPartTemp(qlibid, ssqwt, parthash, 0, 0, &size);
    if (ret == 0){
        printf("Error: could not read Init parthash %s", parthash);
        return -1;
    }

    if (isInit){
        char* kvRet = KVGetTemp(qlibid, ssqwt, "manifest.dash");
        if (kvRet == 0){
            printf("Error could not find manifest in fabric\n");
            return -3;
        }
        char* segmentTimeline = strstr(kvRet, "<SegmentTimeline>");
        if (segmentTimeline == 0){
            printf("Error: could not find <SegmentTimeline> in manifest\n");
            return -2;
        }
        char* expr = "<S t=\"";
        char* tequals = strstr(segmentTimeline, expr);
        if (tequals == 0){
            printf("Error: could not find %s in manifest\n", expr);
            return -4;
        }
        //char segmentName[32];
        tequals += strlen(expr);
        char* segend = strstr(tequals, "\"");
        strncpy(segmentName, tequals, segend-tequals);
    }
    // ret has init segment must write file when all success
    // get the mpd for 1st segement name
    snprintf(keybuf, keysz, "segment.%s.%s", isVideo ? "video" : "audio", segmentName);
    char* firstPartHash = KVGetTemp(qlibid, ssqwt, keybuf);
    if (firstPartHash == 0){
        printf("Error: could not find %s \n", keybuf);
        return -18;
    }
    char* retFirst = QReadPartTemp(qlibid, ssqwt, firstPartHash, 0, 0, &sizeFirst);
    if (retFirst == 0){
        printf("Error: could not read %s parthash %s", segmentName, parthash);
        return -1;
    }
    //const char* initSeg = "init.m4s";
    char concatFile[512];
    sprintf(concatFile, "concat-live-%s-%s-data.m4s", isVideo ? "video" : "audio",segmentName);


    // these need to be merged and written
    char* combined = malloc(size+sizeFirst);
    memcpy(combined, ret, size);
    memcpy(combined+size, retFirst, sizeFirst);
    printf("concatfile = %s size = %d sizeFirst = %d\n", concatFile, size,sizeFirst);
    char tempDirName[2048];
    strcpy(tempDirName, "temp/tempXXXXXXXX");
    char* retDir = createTempDir(tempDirName);
    if (retDir == 0){
        printf("Error: could not create temp dir %s\n", tempDirName);
        return -10;
    }
    printf("TEMPDIR = %s\n!!!!!!", tempDirName);
    int r = bufferToFile(combined, concatFile,  tempDirName, size + sizeFirst);
    free(combined);
    if (r != 0){
        printf("Error: failed to write concatenated file %s\n", concatFile);
        return r;
    }


    char newIValue[1024];
    char newBValue[1024];
    char newScaleValues[1024];
    char newInitFilename[1024];
    char dummyName[1024];

    sprintf(newIValue, inputArrayVal[4], tempDirName, concatFile);
    printf("VALUE=%s\n", newIValue);
    inputArrayVal[4] = newIValue;
    sprintf(newBValue, inputArrayVal[8], format->Bandwidth);
    inputArrayVal[8] = newBValue;
    sprintf(newScaleValues, inputArrayVal[9], format->Width, format->Height);
    inputArrayVal[9] = newScaleValues;

    if (isInit){
        sprintf(newInitFilename, initTemplate, format->Width, format->Height, format->Bandwidth);
        inputArrayVal[16] = newInitFilename;
        inputArrayVal[17] = dummyFile;
        char dummyName[1024];
        sprintf(dummyName, inputArray[18], tempDirName);
        inputArray[18] = dummyName;
    }
    else{
        inputArrayVal[16] = dummyFile;
        sprintf(newInitFilename, segTemplate, format->Width, format->Height, format->Bandwidth, segmentName   );
        inputArrayVal[17] = newInitFilename;
        sprintf(dummyName, inputArray[18], tempDirName);
        inputArray[18] = dummyName;
    }

    char keyArray[8192];
    char valArray[8192];

    int bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
    int bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));
    int res =  (int) FFMPEGRun(keyArray, bufLen, valArray, bufValLen);

    if (res != 0){
        return Error("FFPEGRun Failed", res);
    }

    res = loadFromFileAndPackData(outbufsize, outbuf,newInitFilename, concatFile, isVideo ? "video/mp4" : "audio/mp4",tempDirName);
    if (res == 0) //leave error files behind for now
        removeTempDir(tempDirName);
    return res;
}



/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   outbufsize - output size of buffer
 *   outbuf - buffer of size (outbufsize)
 *   segment - requested segment (timestamp)
 *   isVideo - video or audio
 * Return:
 *   0 is success else failure
 *
 *  load the raw segment from fabric
 *
 *
 */
int dashSegmentRaw(char* qlibid, char* qhash, int* outbufsize, char *outbuf, char *segment, int isVideo ){
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Error : Could not find segment storer\n");
        return -1;
    }
    const int keysz = 1024;
    char keybuf[keysz];
    snprintf(keybuf, keysz, "segment.%s.%s",(isVideo ? "video" : "audio"), segment);
#if VERBOSE_LEVEL >= 2
    printf("looking for part in temp=%s\n",keybuf);
#endif
    char* parthash = KVGetTemp(qlibid, ssqwt, keybuf);
    if (parthash){
        uint32_t size=0;
        printf("part %s found retrieving data\n", keybuf);
        printf("parthash = %s\n", parthash);
        char* ret = QReadPartTemp(qlibid, ssqwt, parthash, 0, 0, &size);
        *outbufsize = size;
#if VERBOSE_LEVEL >= 2
        printf("size = %d\n", *outbufsize);
#endif
        if (ret != 0){
            memcpy(outbuf, ret, *outbufsize);
#if VERBOSE_LEVEL >= 2
            printf("Success retrieving raw segment size %d\n", *outbufsize);
#endif
            return 0;
        }
        else{
            printf("Error : No Temp part found for hash = %s\n", parthash);
            return -5;
        }
    }
    printf("part for segment %s not found FAILING\n", segment);
    return -1;
}

/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   outbufsize - output size of buffer
 *   outbuf - buffer of size (outbufsize)
 * Return:
 *   pointer to manifest string, 0 on failure
 *
 *  load the raw manifest from fabric
 *
 *
 */
char* dashManifestRaw(char* qlibid, char* qhash, int outbufsize, char *outbuf)
{
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Failed to find segment storer\n");
        return NULL;
    }
    char* kvRet = KVGetTemp(qlibid, ssqwt, "manifest.dash");
    if (kvRet == NULL) {
        printf("Failed to find dash manifest\n");
        return NULL;
    } else if (strlen(kvRet) > outbufsize){
        printf("Storage requires more than allocated\n");
        return NULL;
    }
    strncpy(outbuf, kvRet, strlen(kvRet)+1);
    return outbuf;
}

/*
 * Arguments:
 *   bufbegin string - start of input
 *   tag string - what to look for
 *   pbuf - buffer to output to
 *   expand - expansion for tag
 * Return:
 *   pointer in pbuf to end of expansion
 *
 *  expands a replacement tag to another string into pbuf
 *
 *
 */
char* expandNextProperty(char* bufbegin, char* tag, char* pbuf, char* expand){
    char* bwpos = strstr(bufbegin, tag);
    if (bwpos == 0){
        printf("Failed to find %s begin\n", tag);
        return 0;
    }
    strncpy(pbuf, bufbegin, bwpos-bufbegin); // up to tag
    pbuf += bwpos-bufbegin;
    strcpy(pbuf, tag);
    pbuf += strlen(tag);
    strcpy(pbuf, expand);
    char* end =  strstr(bwpos, "\"") + 1; //other end of tag
    if (end == 0) {
        printf("didn't find end string\n");
        return 0;
    }
    return end + strlen(expand);
}

/*
 * Arguments:
 *   begin string - beginning of buffer to scan
 *   ch - character to replace s in m4s
 *
 *  replaces the s in m4s with ch
 *  will replace all m4s to m4(ch) in the buffer (not just once)
 *
 */
void replaceFilenameTerm(char* begin, char ch){
    const char* ext = ".m4s";
    for (int j = 0;j < 2; j++){
        char* sToV = strstr(begin, ext);
        if (sToV == 0){
            printf("Counld not find %s in %s\n", ext,begin);
            return;
        }
        sToV[strlen(ext)-1] = ch;
        begin = sToV + strlen(ext);
    }
}

/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   outbufsize - output size of buffer
 *   outbuf - buffer of size (outbufsize)
 * Return:
 *   pointer to manifest string, 0 on failure
 *
 *  load the manifest from fabric using parameters for
 *  formats from the fabric
 *      vidformats.width
 *      vidformats.height
 *      vidformats.bw
 */
char* dashManifest(char* qlibid, char* qhash, int outbufsize, char *outbuf){
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Failed to find segment storer\n");
        return NULL;
    }
    char* kvRet = KVGetTemp(qlibid, ssqwt, "manifest.dash");
    if (kvRet == NULL) {
        printf("Failed to find dash manifest");
        return NULL;
    } else if (strlen(kvRet) > outbufsize){
        printf("Storage requires more than allocated\n");
        return NULL;
    }
    int cbManifest = strlen(kvRet);

    const int manifestExtra = 16384; // extra space to allocate to modify manifest
    char* return_buffer = malloc(cbManifest + manifestExtra); // return buffer
    char* pbuf = return_buffer; // current buffer pointer

    // These variables maintain the video configuration (width x height x bandwidth)
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

    if (nwelems != nhelems || nwelems != nbwelems) {
        if (welemv) freeargv(nwelems, welemv, welemsz);
        if (helemv) freeargv(nhelems, helemv, helemsz);
        if (bwelemv) freeargv(nbwelems, bwelemv, bwelemsz);
        if (return_buffer) free(return_buffer);
        return 0;
    }
    /*
    We are about to look for the first tag of interest

	<Period id="0" start="PT0.0S">
		<AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true">

    */
    char* pAdaptation = strstr(kvRet, "<AdaptationSet");
    if (pAdaptation == 0){
        printf("Failed to find AdaptionSet begin\n");
        outbuf = 0;
        goto CleanUp;
    }
    char* endAdapt = strstr(pAdaptation, ">"); // found our AdaptationSet
    if (endAdapt == 0){
        printf("Failed to find AdaptionSet begin\n");
        outbuf = 0;
        goto CleanUp;
    }
    // copy <AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true"> to output
    strncpy(pbuf, kvRet, (endAdapt+1)-kvRet);
    pbuf += (endAdapt+1)-kvRet; // move insert head

#if VERBOSE_LEVEL >= 2
    printf("ret = %s\n", return_buffer);
#endif
    char* endVideo = 0; //End of the video section in manifest

    const char* repbeginTag = "<Representation id=\""; //tag for begin rep

    // The following loop handles each Representation (transcode config)
    // Currently we have 2 supported resolutions but this loop
    // accomodates any number
    for (int i=0; i<nbwelems; i++){
    // find our first begin replacement
        char triplet[2048];
        sprintf(triplet, "%sx%s-%s", welemv[i], helemv[i], bwelemv[i]);

#if VERBOSE_LEVEL >= 2
        printf("triplet %s\n",triplet);
#endif

        char* repbegin = strstr(endAdapt, repbeginTag);
        if (repbegin == 0){
            printf("Failed to find Representation 0\n");
            outbuf = 0;
            goto CleanUp;
        }
        strncpy(pbuf, repbegin, strlen(repbeginTag)); //buffer has up to '<Representation id="'
        pbuf += strlen(repbeginTag);
        strcpy(pbuf, triplet); // put triplet
        pbuf += strlen(triplet); // skip past
        repbegin += strlen(repbeginTag) + 1; //skip past 0/1

        // This are we are stitching together the original xml with the
        // original Rep Id=X --> RepId=WxHxBW

        char* bwexpand = expandNextProperty(repbegin, "bandwidth=\"", pbuf, bwelemv[i]);
        if (bwexpand == 0){
            outbuf = 0;
            goto CleanUp;
        }
        pbuf += bwexpand-repbegin; //move insert head
        char* widthexp = expandNextProperty(bwexpand, "width=\"",pbuf, welemv[i]);
        if (widthexp == 0){
            outbuf = 0;
            goto CleanUp;
        }
        pbuf += widthexp-bwexpand; //move insert head
        char* heightexp= expandNextProperty(widthexp, "height=\"",pbuf, helemv[i]);
        if (heightexp == 0){
            outbuf = 0;
            goto CleanUp;
        }
        pbuf += heightexp-widthexp;  //move insert head

        // Move us to the end of this Rep section
        char* endrep = "</Representation>";

        char* end = strstr(heightexp, endrep);
        if (end == 0){
            printf("Failed to find </Representation>\n");
            outbuf = 0;
            goto CleanUp;
        }
        end += strlen(endrep);

#if VERBOSE_LEVEL >= 2
        printf("return_buffer = %s\n", return_buffer);
#endif
        // Move the remainder of the Rep section out to the return buffer
        char* pbufAtHeight = pbuf;
        strncpy(pbuf, heightexp, (end-heightexp));
        pbuf += end-heightexp;
        endVideo = end;
        // replace the m4s to m4v in the section (and forward) we just expanded
        replaceFilenameTerm(pbufAtHeight, 'v');

    }


    // Copy the remainder of the characters from end rep to audio rep as they
    // remain unchanged
    char* audioRep = strstr(endVideo, repbeginTag);
    strncpy(pbuf, endVideo, audioRep-endVideo);
    pbuf += audioRep-endVideo;//move insert head

    // Handle audio need to replace <Representation id="1" with <Representation id="STEREO-128000"
    char* audioTag =  "STEREO-128000";
    if (audioRep == 0){
        printf("Error: could not find audio section\n");
        outbuf = 0;
        goto CleanUp;
    }
    // This block simply replaces the Rep ID as in video with a fixed bandwidth of 128000
    strcpy(pbuf, repbeginTag);
    pbuf += strlen(repbeginTag);//move insert head
    audioRep += strlen(repbeginTag);
    audioRep++; // skip real ID
    strcpy(pbuf,audioTag);
    pbuf += strlen(audioTag);//move insert head

    // remainder of the file
    strcpy(pbuf, audioRep);

    // Must handle true audio replacement just handling filenames now
    // will change all m4s audio tracks to m4a
    replaceFilenameTerm(pbuf, 'a');


    strncpy(outbuf, return_buffer, strlen(return_buffer)+1);

CleanUp:

    if (return_buffer) free(return_buffer);
    if (welemv) freeargv(nwelems, welemv, welemsz);
    if (helemv) freeargv(nhelems, helemv, helemsz);
    if (bwelemv) freeargv(nbwelems, bwelemv, bwelemsz);

    return outbuf;
}

/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   filename - file to load data from (ffmpeg generated)
 *   outbuf - buffer of size (outbufsize)
 *   isVideo - video or audio
 * Return:
 *   parthash of newly created part NULL on failure
 *
 *  load the raw file from disk and delivers to the fabric
 *
 *
 */
char* loadFromFileAndSaveData(char* qlibid, char* qhash, char* filename, int isVideo){
    char targetFilename[1024];
    sprintf(targetFilename, "%s", filename);
    int fsize = 0;
    char* segData = LoadDataFromFile(targetFilename, &fsize);
    if (segData == 0){
        printf("Error: loadFromFileAndSaveData failing from Data load from file\n");
        return 0;
    }
    char ts[128];
    char* tsRet = extractTimestamp(filename, ts);

    if (tsRet == NULL){
        printf("Error: timestamp extraction failed on %s\n", filename);
        return NULL;
    }
    else{
#if VERBOSE_LEVEL >= 2
        printf("timestamp = %s\n", ts);
#endif
    }
    /* Fix ffmpeg segment sequence and base time in place */
    //char* parthash = writeAsPart(qlibid,qwt,name,segData,cb, "segment");
#if VERBOSE_LEVEL >= 3
    printf("Location 1\n");
#endif
    int rc;
    const int keysz = 1024;
    char keybuf[keysz];

    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL){
        printf("Error: ssqwt is NULL!!!\n");
        return 0;
    }

#if VERBOSE_LEVEL >= 3
    printf("Location 2\n");
#endif
    uint32_t usize = (uint32_t)fsize;
    //printf("qlibid=%s ssqwt=%s %d size=%d\n", qlibid, ssqwt,segData[usize-1],usize);
    //creates and finalizes the part
#if VERBOSE_LEVEL >= 3
    char outfile[1024];
    sprintf(outfile, "%s.%s.mee", isVideo?"v":"a", ts);
    FILE *handleWrite=fopen(outfile,"wb");

    /*Writing data to file*/
    fwrite(segData, 1, usize, handleWrite);

    /*Closing File*/
    fclose(handleWrite);
#endif
    char *parthash = QCreatePart(qlibid, ssqwt,segData,usize); // check this !!!!!!!
    if (parthash == NULL){
        printf("Error: parthash is NULL\n");
        return 0;
    }
 #if VERBOSE_LEVEL >= 2
    printf("parthash = %s\n", parthash);
#endif
#if VERBOSE_LEVEL >= 3
    printf("Location 3\n");
#endif

    snprintf(keybuf, keysz, "segment.%s.%s",(isVideo ? "video" : "audio"), ts);
    printf("Part Created %s, partname = %s\n", parthash, keybuf);
    rc = KVSet(qlibid, ssqwt, keybuf, parthash);

#if VERBOSE_LEVEL >= 3
    printf("Location 4\n");
#endif

    free(segData);
    return parthash;
}


/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   filename - file to load data from (ffmpeg generated)
 * void:
 *
 *  load the raw manifest file from disk and delivers to the fabric as a KVSet not a part
 *
 *
 */
void loadManifestAndSaveData(char* qlibid, char* qhash, char* filename){
    int size = 0;
    char* segData = LoadDataFromFile(filename, &size); // this pointer has to be freed
    if (segData == 0){
        printf("Error: loadManifestAndSaveData failing from Data load from file %s\n", filename);
    }
    segData[size]=0;
//   This section changes the 2 properties
//   Its purpose is to limit delay between live broadcast and transcode
    char* suggested_delay = "suggestedPresentationDelay=";
    char* newdelay = "PT0S";
    char* min_buffer_time = "minBufferTime=";
    char* newmin = "PT2.0S";
    char* delay = strstr(segData, suggested_delay);
    delay += strlen(suggested_delay);
    strncpy(delay+1, newdelay, strlen(newdelay));
    char* minbuffer = strstr(segData, min_buffer_time);
    minbuffer += strlen(min_buffer_time);
    strncpy(minbuffer+1, newmin, strlen(newmin));
//  Insert the manifets into the fabric as a KVSet
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == NULL) {
        printf("Error: Could not find segment storer\n");
        return;
    }
    KVSet(qlibid, ssqwt, "manifest.dash", segData);
//  Cleanup
    free(segData);
}


/*
 * Arguments:
 *   qlibid string - the content library id
 *   qwt string - content token
 *   pContentRequest - file being updated by ffmpeg
 * return:
 *  0 on Success else failure
 *  called by FSNotify goroutine upon interesting change to the stream
 *
 *
 */
int ffmpeg_update_cbk(int outsz, char *outbuf, const char *argbuf)
{
#if VERBOSE_LEVEL >= 3
    printf("ffmpeg_update_cbk\n");
#endif
    int argc;
    char **argv;
    uint32_t *argsz;

    argc = buf2argv(argbuf, &argv, &argsz);

    // argc should be 3
    if (argc < 3) return -1;
#if VERBOSE_LEVEL >= 2
    printf("argc=%d lib=%s url=%s\n", argc, argv[0], argv[2]);
#endif
    char *qlibid = argv[0];
    char *qwt = argv[1];
    char* pContentRequest = argv[2];
    char szMK[5];
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    strcpy(szMK, (pContentRequest+cbContentRequest-3));
    if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
        mk = MANIFEST;
    else if (strcmp(szMK, "m4s") == 0){
        mk = (strstr(pContentRequest, "-0-")) ? VIDEO:AUDIO;
    }
    else if (strcmp(szMK, "m4a") == 0)
        mk = AUDIO;
    else if (strcmp(szMK, "tst") == 0)
        mk = TEST;
    else
        return -1;

    switch(mk){
        case MANIFEST:
            loadManifestAndSaveData(qlibid, qwt, pContentRequest);
            break;
        case VIDEO:
            loadFromFileAndSaveData(qlibid, qwt, pContentRequest, 1);
            break;
        case AUDIO:
            loadFromFileAndSaveData(qlibid, qwt, pContentRequest, 0);
            break;
        case TEST:
            printf("Test Not Yet Implemented\n");
            break;
    };
    return 0;
}
/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash to aquire segment storer
 *   pContentRequest string - url to parse attributes from
 *   mk  MKTypes - AUDIO VIDEO TEST MANIFEST
 *   outbuf - buffer of size (outsz)
 * return:
 *  0 on Success else failure
 *
 *  this function really separates the Raw and Transcoding setup from
 *  each other.  This function breaks down the URL and based on mk processes the
 *  request.  Currently AUDIO is Raw.
 *
 */
int processContentRequest(char* qlibid, char* qhash, char* pContentRequest, MKTypes mk, char* outbuf, int outsz){
    printf("IN PROCESS CONTENT\n");
    int cbContentRequest = strlen(pContentRequest);
    char live[16]; //hardcoded for now on live
    char widthheight[40];
    char* width = widthheight;
    char* height;
    char bandwidth[20];
    char dash[20];
    char segment[32];
    char ext[16];
    char* data_segment=0;
    int res = 0;

    switch(mk){
    case VIDEO:
    {
        for (int i=0; i < cbContentRequest;i++){
            char curCh = pContentRequest[i];
            if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                pContentRequest[i] = ' ';
        }

        sscanf(pContentRequest, "%s%s%s%s%s%s\n", dash, live, widthheight, bandwidth, segment, ext);

        int cbWidthHeight = strlen(widthheight);

        for (int i=0;i<cbWidthHeight;i++){
            if (widthheight[i] == 'x'){
                widthheight[i] = 0;
                height = &widthheight[i+1];
            }
        }
        printf("parsed dash=%s live=%s width=%s height=%s bandwidth=%s segment=%s ext=%s\n", dash, live, width, height, bandwidth, segment, ext);

        FormatTriplet format;
        format.Height = height;
        format.Width = width;
        format.Bandwidth = bandwidth;
        res = dashSegment(qlibid, qhash, outsz,outbuf, &format, segment);
    }
    break;
    // AUDIO for now, is effetivly raw
    case AUDIO:
    {
        for (int i=0; i < cbContentRequest;i++){
            char curCh = pContentRequest[i];
            if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                pContentRequest[i] = ' ';
        }

        sscanf(pContentRequest, "%s%s%s%s%s%s\n", dash, live, widthheight, bandwidth, segment, ext);

        printf("parsed dash=%s live=%s type=%s bandwidth=%s segment=%s ext=%s\n", dash, live, widthheight, bandwidth, segment, ext);

        int bufSize = 1024*1000;
        data_segment = malloc(bufSize);
        if (data_segment == 0){
            printf("Error: Out of Memory\n");
            return -8;
        }
        res = dashSegmentRaw(qlibid, qhash, &bufSize,data_segment,segment, 0);
        char *outargv[2];
        uint32_t outargsz[2];
        outargv[0] = "audio/mp4"; /* c */
        outargsz[0] = strlen(outargv[0]);
        outargv[1] = data_segment;
        outargsz[1] = bufSize;
        argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);
        free(data_segment);
        data_segment = 0;
    }
    break;
    case MANIFEST:
    {
        *(pContentRequest+cbContentRequest-4) = 0;
        char buf[100 * 1024];
        char *outargv[2];
        uint32_t outargsz[2];
        outargv[0] = "application/xml"; /* c */
        outargsz[0] = strlen(outargv[0]);
        printf("CALLING DASH MANIFEST\n");
        char *bufptr = dashManifest(qlibid, qhash, sizeof(buf), buf);
        if (bufptr == 0){
            printf("Error: Unable to load Manifest\n");
            return -11;
        }
        /* Prepare output */
        outargv[1] = buf;
        outargsz[1] = strlen(buf);
        argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);
    }
    break;
    case TEST:
        printf("Test not yet implemented\n");
        break;
    default:
        res = -1;
        break;
    };
    if (data_segment)
        free(data_segment);

    return 0;

}
/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash to aquire segment storer
 * return:
 *  0 on Success else failure
 *  begins the live stream
 *
 *
 */
int processContentRequestRaw(char* qlibid, char* qhash, char* pContentRequest, MKTypes mk, char* outbuf, int outsz){

    int cbContentRequest = strlen(pContentRequest);
    char* szPreamble = alloca(cbContentRequest);
    char raw[16]; //hardcoded for now on live
    char type[20];
    char contents[10];
    char track[10];
    char ext[16];
    char* data_segment=0;
    int res = 0;

    switch(mk){
    case VIDEO:
    case AUDIO:
    {
        // int isInit = (strstr(pContentRequest, "init") != 0);
        raw[0] = 0;
        for (int i=0; i < cbContentRequest;i++){
            char curCh = pContentRequest[i];
            if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                pContentRequest[i] = ' ';
        }

        sscanf(pContentRequest, "%s%s%s%s%s%s\n", szPreamble, raw, type, contents, track, ext);
        printf("parsed pre=%s raw=%s type=%s contents=%s track=%s ext=%s", szPreamble, raw, type, contents, track, ext);

        int bufSize = 4*1024*1000;
        data_segment = malloc(bufSize);
        res = dashSegmentRaw(qlibid, qhash, &bufSize, data_segment,track,mk==VIDEO?1:0);
        if (res != 0){
            printf("Error: dashSegmentRaw failed code=%d\n", res);
        }
        char *outargv[2];
        uint32_t outargsz[2];
        outargv[0] = mk==VIDEO?"video/mp4":"audio/mp4"; /* c */
        outargsz[0] = strlen(outargv[0]);
        outargv[1] = data_segment;
        outargsz[1] = bufSize;
        argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);

    }
    break;
    case MANIFEST:
    {
        *(pContentRequest+cbContentRequest-4) = 0;


        char buf[100 * 1024];
        char *outargv[2];
        uint32_t outargsz[2];
        outargv[0] = "application/xml"; /* c */
        outargsz[0] = strlen(outargv[0]);
        char *bufptr = dashManifestRaw(qlibid, qhash, sizeof(buf), buf);
        if (bufptr == NULL) {
            // FIXME clean up and return
        }
        /* Prepare output */
        outargv[1] = buf;
        outargsz[1] = strlen(buf);
        argv2buf(2, (const char **)outargv, outargsz, outbuf, outsz);
    }
    break;
    case TEST:
        printf("Test not yet implemented\n");
        break;
    default:
        res = -1;
        break;
    };
    if (data_segment)
        free(data_segment);

    return 0;
}

/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash to aquire segment storer
 * return:
 *  0 on Success else failure
 *  begins the live stream
 *
 *
 */
int start_stream(int outsz, char *outbuf, const char *argbuf)
{
#if VERBOSE_LEVEL >= 3
    printf ("start_stream\n");
#endif
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;

    argc = buf2argv(argbuf, &argv, &argsz);

#if VERBOSE_LEVEL >= 2
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i ++) {
        printf("arg%d (%d) = %.16s\n", i, argsz[i], argv[i]);
    }
#endif
    char* qlibid = argv[0];
    char* qhash = argv[1];
    int port = 0;

    const char* inputArray[] = { "-loglevel"   , "-hide_banner","-y", "-f"          ,"-video_size", "-i", "-avoid_negative_ts", "-acodec","-b:a",  "-vcodec","-vf"          ,"-profile:v","-x264opts"                               ,"-b:v", "-f" , "-adaptation_sets"              , "-min_seg_duration", "-use_timeline", "-use_template", "-remove_at_exit", "-init_seg_name"                   , "-media_seg_name", "./temp/live.mpd"};
    const char* inputArrayVal[] = { "debug"    ,""             , "" , "avfoundation", "640x480"   , "0:Built-in Microphone", "make_zero"          , "aac"     , "128k", "libx264", "format=yuv420p","high"      , "keyint=60:min-keyint=60:scenecut=-1", "500k","dash", "id=0,streams=v id=1,streams=a", "2000000"          , "1"            , "1"            , "0"              ,"live-$RepresentationID$-init.m4s"  , "live-$RepresentationID$-$Time$.m4s", ""};


    const char* inputArrayPort[] = { "-loglevel"   , "-hide_banner","-y", "-analyzeduration","-i"                  , "-avoid_negative_ts", "-acodec","-b:a",  "-vcodec","-vf"          ,"-profile:v","-x264opts"                               ,"-max_muxing_queue_size", "-f" , "-adaptation_sets"              , "-min_seg_duration", "-use_timeline", "-use_template", "-remove_at_exit", "-init_seg_name"                   , "-media_seg_name"                  , "./temp/live.mpd"};
    const char* inputArrayValPort[] = { "debug"    ,""             , "" , "20000000"        , "udp://127.0.0.1:%hu", "make_zero"          , "aac"     , "128k", "libx264", "format=yuv420p","high"      , "keyint=60:min-keyint=60:scenecut=-1", "1024"               ,"dash", "id=0,streams=v id=1,streams=a", "2000000"          , "1"            , "1"            , "0"              ,"live-$RepresentationID$-init.m4s"  , "live-$RepresentationID$-$Time$.m4s", ""};

    char keyArray[8192];
    char valArray[8192];
    int bufLen = 0;
    int bufValLen=0;
    char* ssqwt = KVGet( qlibid, qhash, (char*)segment_storer);
    if (ssqwt == 0){
        printf("Error : Could not find segment storer\n");
        return -1;
    }
    char* portstr = KVGetTemp(qlibid, ssqwt, "stream.property.port");
    if (portstr == 0){
        printf("Error: Port cannot be found in state storer\n");
        return -3;
    }
    printf("Port = %s\n", portstr);
    char* pid = KVGetTemp(qlibid, ssqwt, (char*)stream_handle);
    if (pid != 0 && strcmp(pid,"") != 0){
        printf("Error : Start called on running stream %s !!!\n", pid);
        return -2;
    }
    port = atoi(portstr);
    if (port){
        char port_def[1024];
        sprintf(port_def, inputArrayValPort[4], port);
        inputArrayValPort[4] = port_def;
        bufLen = packStringArray(keyArray, inputArrayPort,  sizeof(inputArrayPort)/sizeof(const char*));
        bufValLen = packStringArray(valArray, inputArrayValPort, sizeof(inputArrayValPort)/sizeof(const char*));
    }
    else{
        bufLen = packStringArray(keyArray, inputArray,  sizeof(inputArray)/sizeof(const char*));
        bufValLen = packStringArray(valArray, inputArrayVal, sizeof(inputArrayVal)/sizeof(const char*));
    }
    int i = FFMPEGRunLive(keyArray, bufLen, valArray, bufValLen, "6000", "ffmpeg_update_cbk", qlibid, qhash);
    char szI[16];
    sprintf(szI, "%d", i);
    KVSet(qlibid, ssqwt, (char*)stream_handle, szI);

    //prepare output
    char stringHandle[16];
    char *outargv[1];
    uint32_t outargsz[1];
    sprintf(stringHandle, "%d", i);
    outargv[0] = stringHandle;
    outargsz[0] = strlen(stringHandle);
    printf("NEW ffmpeg handle %d strHandle = %s len = %d\n ", i, stringHandle, outargsz[0] );
    argv2buf(1, (const char **)outargv, outargsz, outbuf, outsz);

#if VERBOSE_LEVEL >= 3
    printf("after argv2buf\n");
#endif
    return 0;
}


/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash to aquire segment storer
 *   pContentRequest - file being requested by dash
 * return:
 *  0 on Success else failure
 *  content retrieval although special case now to allow curl to start and stop stream
 *
 *
 */
int content(int outsz, char *outbuf, const char *argbuf)
{
#if VERBOSE_LEVEL >= 3
    printf("Bitcode content\n");
#endif

    int argc;
    char **argv;
    uint32_t *argsz;
    int res = 0;

    argc = buf2argv(argbuf, &argv, &argsz);

    //argc should be 3
#if VERBOSE_LEVEL >= 2
    printf("argc=%d lib=%s\n", argc, argv[0]);
#endif

    if (argc < 3) {
        printf("Error: Incorrect number of params. Expecting no less than 3 got %d\n", argc);
        return -1;
    }

    char *qlibid = argv[0];
    char *qhash = argv[1];
    char* pContentRequest = argv[2];
    char szMK[5];
    const int isRaw = strstr(pContentRequest, "raw/") != 0;
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    strcpy(szMK, (pContentRequest+cbContentRequest-3));
    if (isRaw){
        if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
            mk = MANIFEST;
        else if (strcmp(szMK, "m4s") == 0){
            mk = (strstr(pContentRequest, "-0-")) ? VIDEO:AUDIO;
        }
        else if (strcmp(szMK, "m4a") == 0)
            mk = AUDIO;
        else if (strcmp(szMK, "tst") == 0)
            mk = TEST;
        else{
            if (strcmp(szMK, "sta") == 0){
                int i = start_stream(outsz,outbuf, argbuf);
                freeargv(argc, argv, argsz);
                return i;
            }
            else{
                int i = stop_stream(outsz,outbuf,argbuf);
                freeargv(argc, argv, argsz);
                return i;
            }
        }
    }
    else{
        if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
            mk = MANIFEST;

        else if (strcmp(szMK, "m4a") == 0)
            mk = AUDIO;
        else if (strcmp(szMK, "m4v") == 0)
            mk = VIDEO;
        else if (strcmp(szMK, "tst") == 0)
            mk = TEST;
        else{
            printf("Unknown content request for extension %s", szMK);
            return -7;
        }

    }

    if (isRaw){
        res = processContentRequestRaw(qlibid, qhash, pContentRequest, mk, outbuf, outsz);
    }
    else{
        res = processContentRequest(qlibid, qhash, pContentRequest, mk, outbuf, outsz);
    }
    return res;
}
