/*
 * AVMASTER2000.IMF - custom content type
 *
 * Implements the AVMASTER2000 specific API.
 *
  Build commands:

  /usr/local/opt/llvm/bin/clang++ -Wall -std=c++11 -I pointer to /nlohman/json/include -fno-exceptions -emit-llvm -fno-use-cxa-atexit -c -g avmaster2000.cpp -o avmaster2000.imf.bc
*/

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
#include <sstream>
#include <nlohmann/json.hpp>
#include <libavformat/avformat.h>
#include "eluvio/dasher.h"
#include "eluvio/cmdlngen.h"
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/dashmanifest.h"
#include "eluvio/dashsegment.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
using namespace elv_context;

using nlohmann::json;



template<char delimiter>
class WordDelimitedBy : public std::string
{};

/*
 *  Renders an html page with a dash player.
 */
std::pair<nlohmann::json, int> dashHtml(BitCodeCallContext* ctx, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char mpdUrl[szUrl];
  strcpy(mpdUrl,url);

  char *ext = strrchr(mpdUrl, '.');
  if(strcmp(ext,".html") != 0)
    return ctx->make_error("make html requested with url not containing .html", -1);

  /* replace html with mpd */
  if (ext != NULL)
      strcpy(ext, ".mpd");

  /*TODO: this embedded html should be a template resource, part of the content */
  const std::string body_format(R"(
    <!doctype html>
    <html>
        <head>
            <title>Eluvio Fabric Dash Player</title>
            <style>
                video{
                    display: block; margin-left: auto; margin-right: auto; height: 100vh; width: auto;
                }
                body {
                    background-color:black;
                }
            </style>
        </head>
      <body>
        <div>
            <video data-dashjs-player autoplay src='%s' controls>
            </video>
        </div>
        <script src='https://cdn.dashjs.org/latest/dash.all.min.js'></script>
      </body>
      </html>)");

    const char* headers = R"("text/html")";

    std::string str_body = string_format(body_format, mpdUrl);
    std::string response_template(R"({"http" : {"status" : %d, "header" : {"Content-Type" : ["%s"], "Content-Length" :  [%d]} }})");
    auto response = string_format(response_template, 200, headers, str_body.length());
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

/*
 * Outputs a single video file based on the start and end times in the request
 * pStart       - string rep of a floating point start time
 * pEnd         - string rep of a floating point end time
 *
 * Video Segment: http://localhost:4567/dash/rep/video?start=22&end=33
 * Video Segment: http://localhost:4567/dash/rep/video?end=52&start=33
 * All temp files are removed on success
 */
std::pair<nlohmann::json, int>
make_video(BitCodeCallContext* ctx, const char* pStart, const char* pEnd, const char* tempFileIn=0)
{
    char* inputsSegment[] = {
        (char*)"-hide_banner",
        (char*)"-nostats",
        (char*)"-y",
        (char*)"-loglevel",
        (char*)"debug",
        (char*)"-i",
        (char*)"%INPUTFILE%",
        (char*)"-ss",
        (char*)"MISSED",
        (char*)"-to",
        (char*)"MISSED",
        (char*)"-c:v",
        (char*)"copy",
        (char*)"-c:a",
        (char*)"copy",
        (char*)"%MEDIA%"};

    char* inputsFullVideo[] = {
        (char*)"-hide_banner",
        (char*)"-nostats",
        (char*)"-y",
        (char*)"-loglevel",
        (char*)"debug",
        (char*)"-i",
        (char*)"%INPUTFILE%",
        (char*)"-ss",
        (char*)"MISSED",
        (char*)"-c:v",
        (char*)"copy",
        (char*)"-c:a",
        (char*)"copy",
        (char*)"%MEDIA%"};

    int szFullParams = sizeof(inputsFullVideo)/sizeof(char*);
    int szSegmentParams = sizeof(inputsSegment)/sizeof(char*);

    char** inputs;
    int cEls;
    bool isFullVideo = false;
    if(pEnd == NULL || pStart == NULL){
        pStart = "0.0";
        inputs = inputsFullVideo;
        cEls = sizeof(inputsFullVideo)/sizeof(const char*);
        isFullVideo = true;
    }else{
        inputs = inputsSegment;
        cEls = sizeof(inputsSegment)/sizeof(const char*);
    }


    auto kvPkg = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"pkg"));

    auto jsonPkg = json::parse(kvPkg.get());
    if (jsonPkg.size() != 1){
        // it should be 1 for now
        std::string err =  "ERROR: PKG has ";
        err += jsonPkg.size();
        err += " entries, should be 1";
        return ctx->make_error(err, -1);
    }

    std::string partHash = *(jsonPkg.begin());

    LOG_INFO(ctx, "PARTHASH=", partHash);
    LOG_INFO(ctx, "JSON=" , jsonPkg);

    if (!isFullVideo){
        inputs[8] = (char*)pStart;
        inputs[10] = (char*)pEnd;
    }else{
        inputs[8] = (char*)pStart;
    }
    nlohmann::json j;
    auto inputStream = ctx->NewStream();
    auto outputStream = ctx->NewStream();
    j["input_hash"] = partHash;
    j["output_stream_id"] = outputStream["stream_id"];
    j["output_media_segment"] = "";

    std::string in_files = j.dump();
    auto ffmpegRet = ctx->FFMPEGRunVideo(inputs, isFullVideo ? szFullParams : szSegmentParams, in_files);
    if (ffmpegRet != 0){
        // ERROR
        return ctx->make_error("FFMpeg failed to run", -2);
    }
    std::vector<unsigned char> segData(300000000);
    auto read_ret = ctx->ReadStream(outputStream["stream_id"], segData);
    if (read_ret.second != 0){
        ctx->Error("Read stream failed", "{}");
        return read_ret;
    }
    int fsize = read_ret.first["read"];

    const char* response_template = R"({"http" : {"status" : %d, "headers" : {"Content-Type" : ["%s"], "Content-Length" :  ["%d"]} }})";
    auto response = string_format(response_template, 200, "video/mp4", fsize);
    nlohmann::json j_callback = json::parse(response);
    ctx->Callback(j_callback);
    auto ret = ctx->WriteOutput(segData, fsize);
    ctx->CloseStream(outputStream["stream_id"]);
    ctx->CloseStream(inputStream["stream_id"]);

    // NOTE IF YOU USE THE PASSED IN FILENAME YOU MUST UNLINK IT YOURSELF
    return ctx->make_success();
}

std::string FormatTimecode ( int time ){
    if ( time < 0 )
        return std::string("ERROR");

    int hour = time / 3600;;
    int min = time % 3600 / 60;;
    int sec = time % 60;

    std::string strRet;

    if ( hour > 0 ){
        if (hour < 10){
            strRet += '0';
        }
        strRet += std::to_string(hour);
        strRet += ':';
    }
    else{
        strRet += "00:";
    }
    if ( min < 60 && min > 0 ){
        if ( min < 10 ){
            strRet += '0';
        }
        strRet += std::to_string(min);
        strRet += ':';
    }else{
        strRet += "00:";
    }
    if ( sec < 60 && sec >= 0 ){
        if ( sec < 10){
            strRet += '0';
        }
        strRet += std::to_string(sec);
    }
    return strRet + ".000";
}

/*
 * Outputs the data from key "image"
 */
std::pair<nlohmann::json, int>
taggit(BitCodeCallContext* ctx, double durationInSecs)
{
    //run --rm --mount src=/home/jan/boatramp,target=/data,type=bind video_tagging:latest boatramp_180s.mov

    char* inputs[] = {
        (char*)"run",
        (char*)"--rm",
        (char*)"--mount",
        (char*)"src=%s,target=/data,type=bind",
        (char*)"video_tagging:latest",
        (char*)"MISSED"
    };

    double roundedUp = ceil(durationInSecs);
    int iRoundedUp = (int)roundedUp;
    int segmentSize = 30;

    int ret = ctx->SQMDSetJSON((char*)"video_tags", (char*)"{}");
    if (ret != 0){
        LOG_ERROR(ctx, "Unable to set video_tags on meta");
        return ctx->make_error("Unable to set video_tags on meta", -1);
    }

    for (int segment=0; segment<iRoundedUp; segment+=segmentSize){
        std::string temp_file_name;

        char buf[1024];
        char bufInner[128];
        sprintf(bufInner, "%s", ffmpeg_elv_constants::dummy_filename_template);
        mktemp(bufInner);
        sprintf(buf, "%s.mp4", bufInner);
        temp_file_name = std::string(buf);
        char CWD[1024];

        if (getcwd(CWD, sizeof(CWD)) == NULL){
            LOG_INFO(ctx, "unable to get full path info for temp");
        }
        std::string fullPath = CWD;
        fullPath += "/temp/";

        std::string fullPathAndFile = fullPath + temp_file_name;
        char start[64];
        char end[64];
        sprintf(start, "%i", segment);
        int iEnd = (segment + segmentSize) > iRoundedUp ? iRoundedUp : (segment + segmentSize);
        sprintf(end, "%i", iEnd);

        LOG_INFO(ctx, "New File = %s begin seg = %s ",  fullPathAndFile, start);

        auto res = make_video(ctx,start,end, (char*)fullPathAndFile.c_str());
        if (res.second != 0){
            LOG_INFO(ctx, "make_video failed for segment beginning %s\nSKIPPING\n", start);
            continue;
        }

        char src_directory[1024];
        sprintf(src_directory, inputs[3], fullPath.c_str());
        inputs[3] = src_directory;
        inputs[5] = (char*)temp_file_name.c_str();

        auto tagRet = ctx->TaggerRun(inputs, sizeof(inputs)/sizeof(char*));

        if (tagRet != 0){
            // ERROR
            return ctx->make_error("TaggerRun failed", tagRet);
        }
        if (GlobalCleanup::shouldCleanup()){
            if (unlink(fullPathAndFile.c_str()) < 0) {
                LOG_ERROR(ctx, "Failed to remove temporary ffmpeg output file %s", temp_file_name.c_str());
            }
        }

        std::string jsonFilename = fullPath + "tag_h.json";

        auto jsonData = FileUtils::loadFromFile(ctx, jsonFilename.c_str());

        if (strcmp(jsonData.get(),(char*)"") != 0){
            LOG_INFO(ctx, jsonData.get());
            auto jsonRep = json::parse(jsonData.get());

            auto video_tags = jsonRep["videoTags"];

            std::string timeIn = FormatTimecode(segment);
            std::string timeOut = FormatTimecode(iEnd);

            json jsonNew = json::array();
            json jsonObj;
            jsonObj["time_in"] = timeIn;
            jsonObj["time_out"] = timeOut;
            jsonObj["tags"] = video_tags;
            jsonNew.push_back(jsonObj);

            int ret = ctx->SQMDMergeJSON((char*)"video_tags", (char*)jsonNew.dump().c_str());
            if (ret != 0){
                LOG_ERROR(ctx, "SQMDMergeJSON failed with code=%d", ret);
                return ctx->make_error("SQMDMergeJSON failed", ret);
            }
        }else{
            LOG_ERROR(ctx, "NO VALID JSON RESULTS RETURNED FROM TAGGER");
        }

    }
    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = "SUCCESS";
    return std::make_pair(j,0);
}

/*
 * Externally callable function to invoke the ML video tagging
 *
 * Arguments:
 *      None (all auto)
 * Example URLs:
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/call/tagger
 */
std::pair<nlohmann::json,int> tagger(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    return taggit(ctx, 100.0);
}


/*
    find_ads(BitcodeCallCtx, std::vector<std::string>& tags)

    tags   - vector of strings with tags of interest
*/
std::pair<nlohmann::json, int> find_ads(BitCodeCallContext* ctx, std::vector<std::string>& tags){

    auto kvPkg = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)"assets"));
    if (kvPkg.get() == NULL){
        LOG_ERROR(ctx, "assets not found in content\n");
        return ctx->make_error("assets not found in content", -1);
    }
    auto jsonPkg = json::parse(kvPkg.get());
    std::map<string, float> matches;
    std::map<string, float>::iterator itProbs;
    float max = 0.0;
    std::string maxTag;

    for(auto& tag : tags){
        for (json::iterator it = jsonPkg.begin(); it != jsonPkg.end(); ++it) {
            auto data = (*it)[tag];
            if (data != NULL){
                if (data > max){
                    max = data;
                    maxTag = tag;
                }
            }
        }
    }

    char returnString[16384];
    auto returnFormat = R"({"price":%.2f,"tag":"%s”})";
    sprintf(returnString, returnFormat, max, maxTag.c_str());

     /* Prepare output */
    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = returnString;
    return std::make_pair(j,0);
}


/*
 *  Returns one ad content object as a JSON { "library" : "", "hash": "" }
 */
std::pair<nlohmann::json,int>  ad(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    /* Extract an ad library from the list */
    auto adlibid = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char *)"eluv.sponsor.qlibid"));

    if (adlibid.get() == NULL) {
        LOG_ERROR(ctx, "Failed to retrieve ad library\n");
        return ctx->make_error("Failed to retrieve ad library", -3);
    }
    auto adhash = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char *)"eluv.sponsor.qhash"));
    if (adlibid.get() == NULL) {
        LOG_ERROR(ctx, "Failed to retrieve ad content hash\n");
        return ctx->make_error("Failed to retrieve ad content hash", -4);
    }

    char *headers = (char *)"text/html";

    char body[1024];
    snprintf(body, sizeof(body), (char *)"{\"library\":\"%s\",\"hash\":\"%s\"}", adlibid.get(), adhash.get());

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    j["body"] = body;
    return std::make_pair(j,0);
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
 *      None all automatic
 *

Example URLs:
  Manifest: http://localhost:4567/dash/EN.mpd
  Video Init Segment: http://localhost:4567/dash/EN-1280x544-1300000-init.m4v
  Video Segment: http://localhost:4567/dash/EN-1280x544-1300000-1.m4v
  Audio Init Segment: http://localhost:4567/dash/EN-STERO-3200-init.m4a
  Audio Segment: http://localhost:4567/dash/EN-STEREO-3200-1.m4a

 */
std::pair<nlohmann::json,int> content(BitCodeCallContext* ctx,  JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    typedef enum eMKTypes { VIDEO , AUDIO, MANIFEST, HTML, AD } MKTypes;

    const char* startTag = "start";
    const char* endTag = "end";

    char* pContentRequest = (char*)(params._path.c_str());

    LOG_INFO(ctx, "content=%s", pContentRequest);
    char szMK[5];
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    strcpy(szMK, (pContentRequest+cbContentRequest-3));

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/image") == 0) {
        LOG_INFO(ctx, "REP /image");
        return make_image(ctx);
    }
    if (strcmp(pContentRequest, "/video") == 0) {
        auto it_start = params._map.find(startTag);
        auto it_end = params._map.find(endTag);
        if (it_end == params._map.end() || it_start == params._map.end()){
            return make_video(ctx, NULL,NULL);
        }

        std::map<std::string, std::string> timeframe;
        timeframe[it_start->first] = it_start->second;
        timeframe[it_end->first] = it_end->second;
        auto startTime = std::stod(timeframe[startTag]);
        auto endTime = std::stod(timeframe[endTag]);
        if (startTime > endTime){
            LOG_ERROR(ctx, "Invalid range specified, end must be greater than start");
            LOG_ERROR(ctx, "start =%d end =%d ", startTime, endTime);
            return ctx->make_error("Invalid range specified, end must be greater than start", -8);;
        }
        return make_video(ctx, timeframe[startTag].c_str(), timeframe[endTag].c_str());
    }
    if (strcmp(pContentRequest, "/ads") == 0) {
        auto it_tags = params._map.find("tags");
        if (it_tags == params._map.end()){
            LOG_ERROR(ctx, "ads requested but no tags provided");
            return ctx->make_error("ads requested but no tags provided", -9);
        }
        std::istringstream iss(it_tags->second);
        std::vector<std::string> tags((std::istream_iterator<WordDelimitedBy<','>>(iss)),
                                        std::istream_iterator<WordDelimitedBy<','>>());
        return find_ads(ctx, tags);
    }

    /* Check for DASH extensions */
    char *dot = strrchr(pContentRequest, '.');
    if (!dot)
        return ctx->make_error("dash request made but not '.' can be found in URL to parse", -9);

    if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
        mk = MANIFEST;
    else if (strcmp(szMK, "m4v") == 0)
        mk = VIDEO;
    else if (strcmp(szMK, "m4a") == 0)
        mk = AUDIO;
    else if (strcmp(dot, ".html") == 0)
        mk = HTML;
    else if (strcmp(dot, ".ad") == 0)
        mk = AD;
    else
        return ctx->make_error("Unknown parse type detected", -10);

//   URL path (e.g.: "/dash/EN-320p@1300000-init.m4v")

    char* szPreamble = (char*)alloca(cbContentRequest);
    char szLanguage[10];
    char szFormat[20];
    char bandwidth[10];
    char track[10];
    json j;

    szLanguage[0] = 0;
    for (int i=0; i < cbContentRequest;i++){
        char curCh = pContentRequest[i];
        if (curCh == '-' || curCh == '/' || curCh == '.' || curCh == '@') // Not found lang yet
            pContentRequest[i] = ' ';
    }
    sscanf(pContentRequest, "%s%s%s%s%s%s", szPreamble, szLanguage, szFormat, bandwidth, track, szMK);

    LOG_INFO(ctx, "content request = %s", pContentRequest);
    char buf[128];
    sprintf(buf, "offering.%s", szLanguage);

    auto kvGotten = CHAR_BASED_AUTO_RELEASE(ctx->KVGet((char*)buf));
    auto kvWatermark = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"watermark"));

    if (kvGotten.get() == NULL){
        LOG_ERROR(ctx, "json NOT FOUND, FAILING");
        return ctx->make_error("json not found", -11);
    }
    std::string encoded_json = kvGotten.get()+1;
    std::string watermark_json = kvWatermark.get() == NULL ? std::string("{}") : kvWatermark.get();
    std::string decoded_json = base64_decode(encoded_json);

    switch(mk){
        case AUDIO:
        case VIDEO:
        {
            Dasher dasher((mk==AUDIO)? 'a' : 'v', std::string(szLanguage), std::string(szFormat), std::string(track), decoded_json, ctx, watermark_json);
            dasher.initialize();
            DashSegmentGenerator gen(dasher);
            return gen.dashSegment(ctx);

        }
        break;
        case MANIFEST:
            {
                j = json::parse(decoded_json);
                DashManifest dashManifest(j);
                std::string manifestString = dashManifest.Create();
                //std::string manifestString = "Hello World!";
                std::string response_template(R"({"http" : {"status" : %d, "headers" : {"Content-Type" : ["application/dash+xml"], "Content-Length" :  ["%d"]} }})");
                auto response = string_format(response_template, 200, manifestString.length());
                nlohmann::json j_response = json::parse(response);
                ctx->Callback(j_response);
                std::vector<unsigned char> manifestData(manifestString.c_str(), manifestString.c_str()+manifestString.length());
                auto ret = ctx->WriteOutput(manifestData);

                return ctx->make_success();

            }
            break;
    case HTML:
        //return dashHtml(ctx, url);
        return ctx->make_error("Feature not currently working", -16);
    case AD:
        return ad(ctx, p);
    default:
        return ctx->make_error("unknown url file extension requested", -15);
    };

    return ctx->make_error("unknown type requested", -17);

}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ad)
    MODULE_MAP_ENTRY(tagger)
END_MODULE_MAP()
