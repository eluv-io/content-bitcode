// * LIVE_RTP - Live real time


#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <ftw.h>

#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
using namespace elv_context;

// Effectively a defered release



// Global strings
const char* state_storer = (char*)"avlive.state.storer";
const char* segment_init = (char*)"segment.init";
const char* stream_handle = (char*)"stream.handle";

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
std::pair<nlohmann::json,int> formatsSet(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    LOG_INFO(ctx, "formatsSet");
    //int rc = 0;

    auto it_video_width = params._map.find("vidformats.width");
    if (it_video_width == params._map.end()){
        LOG_ERROR(ctx, "width not provided");
        return ctx->make_error("vidformats.width", -3);
    }

    auto it_video_height = params._map.find("vidformats.height");
    if (it_video_height == params._map.end()){
        LOG_ERROR(ctx, "height not provided");
        return ctx->make_error("vidformats.height", -4);
    }
    auto it_video_bw = params._map.find("vidformats.bw");
    if (it_video_bw == params._map.end()){
        LOG_ERROR(ctx, "bandwidth not provided");
        return ctx->make_error("vidformats.bw", -5);
    }

    ctx->KVPushBack((char*)it_video_width->first.c_str(), (char*)it_video_width->second.c_str());
    ctx->KVPushBack((char*)it_video_height->first.c_str(), (char*)it_video_height->second.c_str());
    ctx->KVPushBack((char*)it_video_bw->first.c_str(), (char*)it_video_bw->second.c_str());

    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = "SUCCESS";
    return std::make_pair(j,0);
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
std::pair<nlohmann::json,int> stop_stream(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    auto it_libid = params._map.find("qlibid");
    if (it_libid == params._map.end()){
        printf("libid not provided\n");
        return ctx->make_error("libid not provided", -1);
    }

    auto it_qhash = params._map.find("qwtoken");
    if (it_qhash == params._map.end()){
        printf("qhash not provided\n");
        return ctx->make_error("qhash not provided", -2);
    }

    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Failed to find segment storer\n");
        return ctx->make_error("failed to find segment storer", -3);
    }
    auto shandle = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)stream_handle));
    if (shandle.get() == NULL) {
        printf("Failed to find stream handle\n");
        return ctx->make_error("failed to find stream handle", -4);
    }

    // Must remove process handle temp key

    ctx->FFMPEGStopLive(atoi(shandle.get()));
    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = "SUCCESS";
    return std::make_pair(j,0);
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
         *((uint32_t*)(inputBuffer)) = host_network_byte_order::htonl32(len);
        inputBuffer += sizeof(int);
        strcpy(inputBuffer, curString);
        inputBuffer += len;
        bufLen += len + sizeof(int);
    }
    *((uint32_t*)(begin)) = host_network_byte_order::htonl32(stringCount);
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
        segData = (char*)malloc(allocSize);
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
 *  template: string of format "/tmp/tempXXXXXXXX"
 * Return:
 *   pointer to the template input parameter
 *
 *  creates a unique temporary directory at the CWD
 *
 */
char* createTempDir(char* Template){

    char origTemplate[1024];
    int done =0;
    char* tmpDirName = 0;
    strcpy(origTemplate, Template);
    while(!done){
        strcpy(Template, origTemplate);
        tmpDirName = mktemp(Template);

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
std::pair<nlohmann::json,int> loadFromFileAndPackData(BitCodeCallContext* ctx, char* filename, char* origFilename, char* contenttype, char* rootDir){
    int res = 0;
    char szTarget[1024];
    sprintf(szTarget, "%s/%s", rootDir, filename);
    FILE* pTargetFile = fopen(szTarget, "rb");

    if (!pTargetFile){
        return ctx->make_error((char*)"Error: Unable to open", -4);
    }
    fseek(pTargetFile, 0, SEEK_END);
    long fsize = ftell(pTargetFile);
    fseek(pTargetFile, 0, SEEK_SET);  //same as rewind(f);

    char *segData = (char*)malloc(fsize + 1);
    int cb = fread(segData, 1, fsize, pTargetFile);
    if (cb != fsize){
        free(segData);
        fclose(pTargetFile);
        return ctx->make_error("Did not read all the data", -6);
    }
    fclose(pTargetFile);

    sprintf(szTarget, "%s/%s", rootDir, origFilename);
    pTargetFile = fopen(szTarget, "rb");

    if (!pTargetFile){
        return ctx->make_error("Error: Unable to open", -8);
    }
    fseek(pTargetFile, 0, SEEK_END);
    long fsizeOrig = ftell(pTargetFile);
    fseek(pTargetFile, 0, SEEK_SET);  //same as rewind(f);

    char *segDataOrig = (char*)malloc(fsizeOrig + 1);
    cb = fread(segDataOrig, 1, fsizeOrig, pTargetFile);
    if (cb != fsizeOrig){
        free(segData);
        free(segDataOrig);
        fclose(pTargetFile);
        return ctx->make_error("Did not read all the data", -9);
    }
    fclose(pTargetFile);

    if (strstr(filename, "-init") == 0){
        printf("CALLING FIXUP NOW!!!!!!!!!!\n");
        /* Fix ffmpeg segment sequence and base time in place */
        res = FFmpegFixup::ffmpegFixupLive(segData, fsize, segDataOrig, fsizeOrig);
        if (res < 0) {
            free(segData);
            free(segDataOrig);
            return ctx->make_error("Failed to fix segment", -8);
        }
    }

    return ctx->make_success();

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
std::pair<nlohmann::json,int> dashSegment(BitCodeCallContext* ctx,  char* qlibid, char* qhash, FormatTriplet* format, char* segName){
    const char* inputArray[] = { "-hide_banner","-nostats", "-loglevel", "-y", "-i"   , "-copyts", "-map", "-vcodec","-b:v", "-vf"         ,"-x264opts"                           , "-f", "-min_seg_duration", "-use_template", "-use_timeline", "-remove_at_exit", "-init_seg_name"      , "-media_seg_name", "%s/dummy.mpd"};
    const char* inputArrayVal[] = { "",         "",         "debug"    ,""   , "%s/%s", ""        ,"0:v" ,"libx264" , "%s" , "scale=%s:%s ", "keyint=60:min-keyint=60:scenecut=-1", "dash","2000000.0"       , "1"             , "1"           , "0"              , "dummy-0.m4v"       , "%sx%s-%s-%s.m4v"  , ""};

    const char* dummyFile = (char*)"dummy-0.m4v";
    const char* initTemplate = (char*)"%sx%s-%s-init.m4v";
    const char* segTemplate = (char*)"%sx%s-%s-%s.m4v";
    char segmentName[256];


    strcpy(segmentName, segName);

    int isInit = strcmp(segmentName, "init") == 0;
    int isVideo = (format->Height != 0 && format->Width != 0);
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Failed to find segment storer\n");
        return ctx->make_error("Failed to find segment storer",-1);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssqid.get() == NULL) {
        printf("Failed to Content\n");
        return ctx->make_error("Failed to find content",-2);
    }

    const int keysz = 1024;
    char keybuf[keysz];
    char segment_init[128];

    sprintf(segment_init, (char*)"segment.%s.init", isVideo ? (char*)"video" : (char*)"audio");
    auto parthash = CHAR_BASED_AUTO_RELEASE(ctx->KVGet(segment_init));
    if (parthash.get() == 0){
        printf("Error: could not find %s \n", segment_init);
        return ctx->make_error("Failed to find init",-3);
    }
    uint32_t size=0, sizeFirst=0;
    auto ret = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(parthash.get(), 0, -1, &size));
    if (ret.get() == 0){
        printf("Error: could not read Init parthash %s", parthash.get());
        return ctx->make_error("could not read Init parthash",-4);
    }

    if (isInit){

        auto kvRet = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)"manifest.dash"));
        if (kvRet.get() == 0){
            printf("Error could not find manifest in fabric\n");
            return ctx->make_error("Error could not find manifest in fabric",-5);
        }
        char* segmentTimeline = strstr(kvRet.get(), "<SegmentTimeline>");
        if (segmentTimeline == 0){
            printf("Error: could not find <SegmentTimeline> in manifest\n");
            return ctx->make_error("Error could not find <SegmentTimeline> in manifest",-6);
        }
        char* expr = (char*)"<S t=\"";
        char* tequals = strstr(segmentTimeline, expr);
        if (tequals == 0){
            printf("Error: could not find %s in manifest\n", expr);
            return ctx->make_error("Error could not find info in manifest",-7);
        }
        //char segmentName[32];
        tequals += strlen(expr);
        char* segend = strstr(tequals, "\"");
        strncpy(segmentName, tequals, segend-tequals);
    }
    // ret has init segment must write file when all success
    // get the mpd for 1st segement name
    snprintf(keybuf, keysz, "segment.%s.%s", isVideo ? "video" : "audio", segmentName);
    auto firstPartHash = CHAR_BASED_AUTO_RELEASE(ctx->KVGet(keybuf));
    if (firstPartHash.get() == 0){
        printf("Error: could not find %s \n", keybuf);
        return ctx->make_error("Error could not find first part hash",-8);
    }
    auto retFirst = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(firstPartHash.get(), 0, -1, &sizeFirst));
    if (retFirst.get() == 0){
        printf("Error: could not read %s parthash %s", segmentName, parthash.get());
        return ctx->make_error("Error could not read parthash",-9);
    }
    //const char* initSeg = "init.m4s";
    char concatFile[512];
    sprintf(concatFile, "concat-live-%s-%s-data.m4s", isVideo ? "video" : "audio",segmentName);


    // these need to be merged and written
    char* combined = (char*)malloc(size+sizeFirst);
    memcpy(combined, ret.get(), size);
    memcpy(combined+size, retFirst.get(), sizeFirst);
    printf("concatfile = %s size = %d sizeFirst = %d\n", concatFile, size,sizeFirst);
    char tempDirName[2048];
    strcpy(tempDirName, "/tmp/tempXXXXXXXX");
    char* retDir = createTempDir(tempDirName);
    if (retDir == 0){
        printf("Error: could not create temp dir %s\n", tempDirName);
        return ctx->make_error("Error could not create temp dir",-10);
    }
    printf("TEMPDIR = %s\n!!!!!!", tempDirName);
    int r = bufferToFile(combined, concatFile,  tempDirName, size + sizeFirst);
    free(combined);
    if (r != 0){
        printf("Error: failed to write concatenated file %s\n", concatFile);
        return ctx->make_error("Error could not write concatenated file",-11);
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

    char unifiedArray[16384];
    int inputsz = sizeof(inputArray)/sizeof(const char*);
    int combinedsz = inputsz*2;
    const char* combinedArray[combinedsz];

    int head = 0;
    for (int i =0; i< inputsz;i++){
        combinedArray[head] = inputArray[i];
        head++;
        if (*(inputArrayVal[i]) != 0){
            combinedArray[head] = inputArrayVal[i];
            head++;
        }
    }

    int bufLen = packStringArray(unifiedArray, combinedArray,  head);
    int res =  (int) ctx->FFMPEGRun(unifiedArray, bufLen);

    if (res != 0){
        return ctx->make_error("FFPEGRun Failed", res);
    }

    auto load_res = loadFromFileAndPackData(ctx, newInitFilename, concatFile, isVideo ? (char*)"video/mp4" : (char*)"audio/mp4",tempDirName);
    if (load_res.second == 0) //leave error files behind for now
        removeTempDir(tempDirName);
    return load_res;
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
std::pair<nlohmann::json,int> dashSegmentRaw(BitCodeCallContext* ctx, char* qlibid, char* qhash, char *segment, int isVideo ){
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Failed to find segment storer\n");
        return ctx->make_error("Failed to find segment storer",-1);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssqid.get() == NULL) {
        printf("Failed to Content\n");
        return ctx->make_error("Failed to find content",-2);
    }
    const int keysz = 1024;
    char keybuf[keysz];
    snprintf(keybuf, keysz, "segment.%s.%s",(isVideo ? "video" : "audio"), segment);
#if VERBOSE_LEVEL >= 2
    printf("looking for part in temp=%s\n",keybuf);
#endif
    auto parthash = CHAR_BASED_AUTO_RELEASE(ctx->KVGet(keybuf));
    if (parthash){
        uint32_t size=0;
        printf("part %s found retrieving data\n", keybuf);
        printf("parthash = %s\n", parthash.get());
        auto ret = CHAR_BASED_AUTO_RELEASE(ctx->QReadPart(parthash.get(), 0, -1, &size));
#if VERBOSE_LEVEL >= 2
        printf("size = %d\n", *outbufsize);
#endif
        if (ret.get() != 0){
            std::vector<uint8_t> vec(ret.get(), ret.get() + size);
            auto write_res = ctx->WriteOutput(vec);
            if (write_res.second == 0){
    #if VERBOSE_LEVEL >= 2
                printf("Success retrieving raw segment size %d\n", *outbufsize);
    #endif
                nlohmann::json j;
                j["headers"] = "application/json";
                j["body"] = "SUCCESS";
                return std::make_pair(j,0);
            }
        }
        else{
            printf("Error : No Temp part found for hash = %s\n", parthash.get());
            return ctx->make_error("No temp part found", -5);
        }
    }
    printf("part for segment %s not found FAILING\n", segment);
    return ctx->make_error("part for segment not found", -6);
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
std::pair<nlohmann::json, int> dashManifestRaw(BitCodeCallContext* ctx, char* qlibid, char* qhash)
{
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Failed to find segment storer\n");
        return ctx->make_error("Failed to find segment storer", -1);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssqid.get() == NULL) {
        printf("Failed to Content\n");
        return ctx->make_error("Failed to Content", -2);
    }
    auto kvRet = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)"manifest.dash"));
    if (kvRet.get() == NULL) {
        printf("Failed to find dash manifest\n");
        return ctx->make_error("Failed to find dash manifest", -3);
    }
    std::vector<uint8_t> vec(kvRet.get(), kvRet.get() + strlen(kvRet.get()));
    auto write_ret = ctx->WriteOutput(vec);
    if(write_ret.second == 0){
        nlohmann::json j;
        j["headers"] = "application/json";
        j["body"] = "SUCCESS";
        return std::make_pair(j,0);
    }
    return ctx->make_error("Failed to write output", -36);

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
            printf("Could not find %s in %s\n", ext,begin);
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
std::pair<nlohmann::json, int> dashManifest(BitCodeCallContext* ctx, char* qlibid, char* qhash){
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Failed to find segment storer\n");
        return ctx->make_error("Failed to find segment storer", -32);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssid.get() == NULL) {
        printf("Failed to Content\n");
        return ctx->make_error("Failed to Content", -33);
    }

    auto kvRet = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*) "manifest.dash"));
    if (kvRet == NULL) {
        printf("Failed to find dash manifest");
        return ctx->make_error("Failed to find dash manifest", -34);
    }
    // else if (strlen(kvRet.get()) > outbufsize){
    //     printf("Storage requires more than allocated\n");
    //     return NULL;
    // }

    printf("Manifest: %s\n", kvRet.get());
    int cbManifest = strlen(kvRet.get());

    const int manifestExtra = 16384; // extra space to allocate to modify manifest
    auto return_buffer = CHAR_BASED_AUTO_RELEASE((char*)malloc(cbManifest + manifestExtra));
    char* pbuf = return_buffer.get(); // current buffer pointer

    // These variables maintain the video configuration (width x height x bandwidth)
    int nbwelems = 0;
    // char **welemv = NULL;
    // char **helemv = NULL;
    // char **bwelemv = NULL;


    auto width_elems = CHAR_BASED_AUTO_RELEASE(ctx->KVRange((char*)"vidformats.width"));
    //if (width_elems.get() == NULL) width_elems = strdup("");
    ArgumentBuffer widthArgs(width_elems.get());

    auto height_elems = CHAR_BASED_AUTO_RELEASE(ctx->KVRange((char*)"vidformats.height"));
    ArgumentBuffer heightArgs(height_elems.get());

    auto bw_elems = CHAR_BASED_AUTO_RELEASE(ctx->KVRange((char*)"vidformats.bw"));
    ArgumentBuffer bwArgs(bw_elems.get());

    if (widthArgs.Count() != heightArgs.Count() || widthArgs.Count() != bwArgs.Count()) {
        return ctx->make_error("dimensional parameters have different arg sizes", -35);
    }
    else{
        nbwelems = widthArgs.Count();
    }
    /*
    We are about to look for the first tag of interest

	<Period id="0" start="PT0.0S">
		<AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true">

    */
    char* pAdaptation = strstr(kvRet.get(), "<AdaptationSet");
    if (pAdaptation == 0){
        printf("Failed to find AdaptionSet begin\n");
        return ctx->make_error("Failed to find AdaptionSet begin", -36);
    }
    char* endAdapt = strstr(pAdaptation, ">"); // found our AdaptationSet
    if (endAdapt == 0){
        printf("Failed to find AdaptionSet begin\n");
        return ctx->make_error("Failed to find AdaptionSet begin", -37);
    }
    // copy <AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true"> to output
    strncpy(pbuf, kvRet.get(), (endAdapt+1)-kvRet.get());
    pbuf += (endAdapt+1)-kvRet.get(); // move insert head

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
        sprintf(triplet, "%sx%s-%s", widthArgs[i], heightArgs[i], bwArgs[i]);

#if VERBOSE_LEVEL >= 2
        printf("triplet %s\n",triplet);
#endif

        char* repbegin = strstr(endAdapt, repbeginTag);
        if (repbegin == 0){
            printf("Failed to find Representation 0\n");
            return ctx->make_error("Failed to find Representation 0", -38);
        }
        strncpy(pbuf, repbegin, strlen(repbeginTag)); //buffer has up to '<Representation id="'
        pbuf += strlen(repbeginTag);
        strcpy(pbuf, triplet); // put triplet
        pbuf += strlen(triplet); // skip past
        repbegin += strlen(repbeginTag) + 1; //skip past 0/1

        // This are we are stitching together the original xml with the
        // original Rep Id=X --> RepId=WxHxBW

        char* bwexpand = expandNextProperty(repbegin, (char*)"bandwidth=\"", pbuf, bwArgs[i]);
        if (bwexpand == 0){
            return ctx->make_error("Unexpected missing bandwidth", -39);
        }
        pbuf += bwexpand-repbegin; //move insert head
        char* widthexp = expandNextProperty(bwexpand, (char*)"width=\"",pbuf, widthArgs[i]);
        if (widthexp == 0){
            return ctx->make_error("Unexpected missing width", -40);
        }
        pbuf += widthexp-bwexpand; //move insert head
        char* heightexp= expandNextProperty(widthexp, (char*)"height=\"",pbuf, heightArgs[i]);
        if (heightexp == 0){
            return ctx->make_error("Unexpected missing height", -41);
        }
        pbuf += heightexp-widthexp;  //move insert head

        // Move us to the end of this Rep section
        char* endrep = (char*)"</Representation>";

        char* end = strstr(heightexp, endrep);
        if (end == 0){
            printf("Failed to find </Representation>\n");
            return ctx->make_error("Failed to find </Representation>", -42);
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
    char* audioTag =  (char*)"STEREO-128000";
    if (audioRep == 0){
        printf("Error: could not find audio section\n");
        return ctx->make_error("could not find audio section", -43);
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
    std::vector<uint8_t> vec(return_buffer.get(), return_buffer.get() + strlen(return_buffer.get())+1);
    auto write_res = ctx->WriteOutput(vec);
    if (write_res.second == 0){
        nlohmann::json j;
        j["headers"] = "application/json";
        j["body"] = "SUCCESS";
        return std::make_pair(j,0);
    }else{
        return ctx->make_error("Write output failed for manifest", -44);
    }
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
std::pair<nlohmann::json, int>  loadFromFileAndSaveData(BitCodeCallContext* ctx, char* qlibid, char* qhash, char* filename, int isVideo){
    char targetFilename[1024];
    sprintf(targetFilename, "%s", filename);
    int fsize = 0;
    auto segData = CHAR_BASED_AUTO_RELEASE(LoadDataFromFile(targetFilename, &fsize));
    if (segData.get() == 0){
        printf("Error: loadFromFileAndSaveData failing from Data load from file\n");
        return ctx->make_error("loadFromFileAndSaveData failing from Data load from file", -80);
    }
    char ts[128];
    char* tsRet = extractTimestamp(filename, ts);

    if (tsRet == NULL){
        printf("Error: timestamp extraction failed on %s\n", filename);
        return ctx->make_error("ltimestamp extraction failed on file", -81);
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

    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Error: Could not find segment storer\n");
        return ctx->make_error("Could not find segment storer", -82);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssqid.get() == NULL) {
        printf("Error: Could not find QID\n");
        return ctx->make_error("Could not find QID", -83);
    }
    auto ssqhash = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QHASH"));
    if (ssqhash.get() == NULL) {
        printf("Error: Could not find QHASH\n");
        return ctx->make_error("Could not find QHASH", -84);
    }
    auto ssqwt = CHAR_BASED_AUTO_RELEASE(ctx->QModifyContent(ssqid.get(),ssqhash.get()));

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
    auto parthash = CHAR_BASED_AUTO_RELEASE(ctx->QCreatePart(segData.get(),usize)); // check this !!!!!!!
    if (parthash.get() == NULL){
        printf("Error: parthash is NULL\n");
        return ctx->make_error("parthash is null", -85);
    }
 #if VERBOSE_LEVEL >= 2
    printf("parthash = %s\n", parthash.get());
#endif
#if VERBOSE_LEVEL >= 3
    printf("Location 3\n");
#endif

    snprintf(keybuf, keysz, "segment.%s.%s",(isVideo ? "video" : "audio"), ts);
    printf("Part Created %s, partname = %s\n", parthash.get(), keybuf);
    rc = ctx->KVSet(keybuf, parthash.get());
    auto sstoken = CHAR_BASED_AUTO_RELEASE(ctx->QFinalizeContent(ssqwt.get()));

#if VERBOSE_LEVEL >= 3
    printf("Location 4\n");
#endif
    std::vector<uint8_t> vec(sstoken.get(), sstoken.get() + strlen(sstoken.get()));
    auto r = ctx->WriteOutput(vec);
    if (r.second == 0){
        return ctx->make_success();
    }else{
        return ctx->make_error("failed to write token to output stream", -86);
    }
}


/*
 * Arguments:
 *   qlibid string - the content library id
 *   qhash string - content hash
 *   filename - file to load data from (ffmpeg generated)
 * void:
 *
 *  load the raw manifest file from disk and delivers to the fabric as a KVSet not a part
 *char *
 *
 */
std::pair<nlohmann::json, int> loadManifestAndSaveData(BitCodeCallContext* ctx, char* qlibid, char* qhash, char* filename){
    int size = 0;
    auto segData = CHAR_BASED_AUTO_RELEASE(LoadDataFromFile(filename, &size)); // this pointer has to be freed
    if (segData.get() == 0){
        printf("Error: loadManifestAndSaveData failing from Data load from file %s\n", filename);
        return ctx->make_error("loadManifestAndSaveData failing from Data load from file", -55);
    }
    segData.get()[size]=0;
//   This section changes the 2 properties
//   Its purpose is to limit delay between live broadcast and transcode
    char* suggested_delay = (char*)"suggestedPresentationDelay=";
    char* newdelay = (char*)"PT0S";
    char* min_buffer_time = (char*)"minBufferTime=";
    char* newmin = (char*)"PT2.0S";
    char* delay = strstr(segData.get(), suggested_delay);
    delay += strlen(suggested_delay);
    strncpy(delay+1, newdelay, strlen(newdelay));
    char* minbuffer = strstr(segData.get(), min_buffer_time);
    minbuffer += strlen(min_buffer_time);
    strncpy(minbuffer+1, newmin, strlen(newmin));
//  Insert the manifets into the fabric as a KVSet
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)state_storer));
    if (ssid.get() == NULL) {
        printf("Error: Could not find segment storer\n");
        return ctx->make_error("Could not find segment storer", -56);
    }
    auto ssqid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QID"));
    if (ssqid.get() == NULL) {
        printf("Error: Could not find QID\n");
        return ctx->make_error("Could not find QID", -57);
    }
    auto ssqhash = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"QHASH"));
    if (ssqhash.get() == NULL) {
        printf("Error: Could not find QHASH\n");
        return ctx->make_error("Could not find QHASH", -58);
    }
    auto ssqwt = CHAR_BASED_AUTO_RELEASE(ctx->QModifyContent(ssqid.get(), ssqhash.get()));
    if (ssqwt.get() == NULL) {
        printf("Error: Failed to aquire modify token\n");
        return ctx->make_error("Failed to aquire modify token", -59);
    }
    ctx->KVSet((char*)"manifest.dash", segData.get());
    auto sstoken = CHAR_BASED_AUTO_RELEASE(ctx->QFinalizeContent(ssqwt.get()));
    if (sstoken.get() == NULL) {
        printf("Error: Failed to Finalize content\n");
        return ctx->make_error("Failed to Finalize content", -60);
    }
    return ctx->make_success();
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
std::pair<nlohmann::json,int> ffmpeg_update_cbk(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
#if VERBOSE_LEVEL >= 3
    printf("ffmpeg_update_cbk\n");
#endif
    auto it_libid = params._map.find("qlibid");
    if (it_libid == params._map.end()){
        printf("libid not provided\n");
        return ctx->make_error("libid not provided", -1);
    }
    auto it_qhash = params._map.find("qwtoken");
    if (it_qhash == params._map.end()){
        printf("qhash not provided\n");
        return ctx->make_error("qhash not provided", -2);
    }
    char* qlibid = (char*)(it_libid->second.c_str());
    char *qhash = (char*)(it_qhash->second.c_str());
    char* pContentRequest = (char*)(params._path.c_str());

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
        return ctx->make_error("unknown type requested", -73);

    switch(mk){
        case MANIFEST:
            return loadManifestAndSaveData(ctx, qlibid, qhash, pContentRequest);
            break;
        case VIDEO:
            return loadFromFileAndSaveData(ctx, qlibid, qhash, pContentRequest, 1);
            break;
        case AUDIO:
            return loadFromFileAndSaveData(ctx, qlibid, qhash, pContentRequest, 0);
            break;
        case TEST:
            printf("Test Not Yet Implemented\n");
            return ctx->make_error("Test not yet implemented", -70);
    default:
        return ctx->make_error("unknown url file extension requested", -71);
    };

    return ctx->make_error("unknown type requested", -72);
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
std::pair<nlohmann::json,int> processContentRequest(BitCodeCallContext* ctx, char* qlibid, char* qhash, char* pContentRequest, MKTypes mk){
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
        return dashSegment(ctx, qlibid, qhash, &format, segment);
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
        auto data_segment = CHAR_BASED_AUTO_RELEASE((char*)malloc(bufSize));
        if (data_segment.get() == 0){
            printf("Error: Out of Memory\n");
            return ctx->make_error("memory allocation failure for buffer", -99);
        }
        return dashSegmentRaw(ctx, qlibid, qhash, data_segment.get(), 0);

    }
    break;
    case MANIFEST:
    {
        return dashManifest(ctx, qlibid, qhash);
    }
    break;
    case TEST:
        printf("Test not yet implemented\n");
        return ctx->make_error("Test not yet implemented", -98);
    default:
        return ctx->make_error("default case hit unexpectedly", -97);
    };
    return ctx->make_error("default RETURN hit unexpectedly", -96);
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
std::pair<nlohmann::json,int> processContentRequestRaw(BitCodeCallContext* ctx, char* qlibid, char* qhash, char* pContentRequest, MKTypes mk){

    int cbContentRequest = strlen(pContentRequest);
    char* szPreamble = (char*)alloca(cbContentRequest);
    char raw[16]; //hardcoded for now on live
    char type[20];
    char contents[10];
    char track[10];
    char ext[16];

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

        auto dash_res = dashSegmentRaw(ctx, qlibid, qhash, track,mk==VIDEO?1:0);
        if (dash_res.second != 0){
            printf("Error: dashSegmentRaw failed code=%d\n", dash_res.second);
            return ctx->make_error("dashSegmentRaw failed code",  dash_res.second);
        }
        return dash_res;

    }
    break;
    case MANIFEST:
    {
        return dashManifestRaw(ctx, qlibid, qhash);
    }
    break;
    case TEST:
        printf("Test not yet implemented\n");
        return ctx->make_error("Test not yet implemented", -71);
    default:
        return ctx->make_error("default case hit unexpectedly", -72);
    };
    return ctx->make_error("default RETURN hit unexpectedly", -73);
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
std::pair<nlohmann::json,int> start_stream(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
#if VERBOSE_LEVEL >= 3
    printf ("start_stream\n");
#endif
    char *qlibid = (char*)"NOLIB";
    char *qhash = (char*)"NOHASH";
    int port = 0;

    const char* inputArray[] = { "-loglevel"   , "-hide_banner","-y", "-f"          ,"-video_size", "-i", "-avoid_negative_ts", "-acodec","-b:a",  "-vcodec","-vf"          ,"-profile:v","-x264opts"                               ,"-b:v", "-f" , "-adaptation_sets"              , "-min_seg_duration", "-use_timeline", "-use_template", "-remove_at_exit", "-init_seg_name"                   , "-media_seg_name", "/tmp/live.mpd"}; // FIX THIS IT NUST GO TO ITS OWN DIR!!! JPF
    const char* inputArrayVal[] = { "debug"    ,""             , "" , "avfoundation", "640x480"   , "0:Built-in Microphone", "make_zero"          , "aac"     , "128k", "libx264", "format=yuv420p","high"      , "keyint=60:min-keyint=60:scenecut=-1", "500k","dash", "id=0,streams=v id=1,streams=a", "2000000"          , "1"            , "1"            , "0"              ,"live-$RepresentationID$-init.m4s"  , "live-$RepresentationID$-$Time$.m4s", ""};


    const char* inputArrayPort[] = { "-loglevel"   , "-hide_banner","-y", "-analyzeduration","-i"                  , "-avoid_negative_ts", "-acodec","-b:a",  "-vcodec","-vf"          ,"-profile:v","-x264opts"                               ,"-max_muxing_queue_size", "-f" , "-adaptation_sets"              , "-min_seg_duration", "-use_timeline", "-use_template", "-remove_at_exit", "-init_seg_name"                   , "-media_seg_name"                  , "/tmp/live.mpd"}; // FIX THIS IT NUST GO TO ITS OWN DIR!!! JPF
    const char* inputArrayValPort[] = { "debug"    ,""             , "" , "20000000"        , "udp://127.0.0.1:%hu", "make_zero"          , "aac"     , "128k", "libx264", "format=yuv420p","high"      , "keyint=60:min-keyint=60:scenecut=-1", "1024"               ,"dash", "id=0,streams=v id=1,streams=a", "2000000"          , "1"            , "1"            , "0"              ,"live-$RepresentationID$-init.m4s"  , "live-$RepresentationID$-$Time$.m4s", ""};

    char keyArray[8192];
    char valArray[8192];
    int bufLen = 0;
    int bufValLen=0;
    auto ssid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet( (char*)state_storer));
    if (ssid.get() == 0){
        printf("Error : Could not find segment storer\n");
        return ctx->make_error("Could not find segment storer",-3);
    }
    auto portstr = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)"stream.property.port"));
    if (portstr.get() == 0){
        printf("Error: Port cannot be found in state storer\n");
        return ctx->make_error("Port cannot be found in state storer",-4);
    }
    printf("Port = %s\n", portstr.get());
    auto pid = CHAR_BASED_AUTO_RELEASE(ctx->QSSGet(ssid.get(), (char*)stream_handle));
    if (pid.get() != 0 && strcmp(pid.get(),"") != 0){
        printf("Error : Start called on running stream %s !!!\n", pid.get());
        return ctx->make_error("Start called on running stream",-5);
    }
    port = atoi(portstr.get());
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
    int i = ctx->FFMPEGRunLive(keyArray, bufLen, valArray, bufValLen, (char*)"6000", (char*)"ffmpeg_update_cbk", qlibid, qhash);
    char szI[16];
    sprintf(szI, "%d", i);
    ctx->QSSSet(ssid.get(), (char*)stream_handle, szI);


    //prepare output
    char stringHandle[16];
    sprintf(stringHandle, "%d", i);
    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = stringHandle;
    return std::make_pair(j,0);
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
std::pair<nlohmann::json,int> content(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
#if VERBOSE_LEVEL >= 3
    printf("Bitcode content\n");
#endif

    char *qlibid = (char*)"NOLIB";
    char *qhash = (char*)"NOHASH";
    printf("ADSMG lib=%s qhash=%s\n", qlibid, qhash);fflush(stdout);
    char* pContentRequest =(char*)params._path.c_str();
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
                return start_stream(ctx, p);
            }
            else{
                return stop_stream(ctx,p);
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
            return ctx->make_error("Unknown content request extension", -7);
        }

    }

    if (isRaw){
        return processContentRequestRaw(ctx, qlibid, qhash, pContentRequest, mk);
    }
    else{
        return processContentRequest(ctx, qlibid, qhash, pContentRequest, mk);
    }
}


BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ffmpeg_update_cbk)
    MODULE_MAP_ENTRY(formatsSet)
END_MODULE_MAP()

