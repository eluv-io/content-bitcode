/*
 * The same as avmaster, but the video function creates a much shorter clip instead
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
#include "eluvio/dasher.h"
#include "eluvio/cmdlngen.h"
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/dashmanifest.h"
#include "eluvio/dashsegment.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/media.h"

using namespace elv_context;

using nlohmann::json;



template<char delimiter>
class WordDelimitedBy : public std::string
{};

/*
 *  Renders an html page with a dash player.
 */
elv_return_type dashHtml(BitCodeCallContext* ctx, char *url)
{

  size_t szUrl = strlen(url) + 1;
  char mpdUrl[szUrl];
  strcpy(mpdUrl,url);

  char *ext = strrchr(mpdUrl, '.');
  if(strcmp(ext,".html") != 0){
        const char* msg = "make html requested with url not containing .html";
        return ctx->make_error(msg, E(msg));
  }
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
    ctx->Callback(200,headers,str_body.length());
    std::vector<unsigned char> htmlData(str_body.c_str(), str_body.c_str() + str_body.length());
    auto ret = ctx->WriteOutput(htmlData);
    if (ret.second.IsError()){
        const char* msg = "WriteOutput";
        return ctx->make_error(msg, ret.second);
    }
    return ctx->make_success();
}
/*
 * Outputs the data from key "image"
 */
elv_return_type make_image(BitCodeCallContext* ctx)
{
    return elv_media_fns::make_image(ctx);
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
elv_return_type
make_video(BitCodeCallContext* ctx, const char* pStart, const char* pEnd, nlohmann::json* output=NULL, const char* filename=NULL)
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
        (char*)"-vf",
        (char*)R"(select='lt(mod(t\,10)\,0.5)',setpts=N/FRAME_RATE/TB)",
        (char*)"-af",
        (char*)R"(aselect='lt(mod(t\,10)\,0.5)',asetpts=N/SR/TB)",
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


    auto kvPkg = ctx->SQMDGetJSON((char*)"pkg");
    if (kvPkg.second.IsError()){
        const char* msg = "SQMD find pkg";
        return ctx->make_error(msg, kvPkg.second);
    }

    auto jsonPkg = kvPkg.first;

    std::string partHash = *(jsonPkg.begin());

    LOG_DEBUG(ctx, "make_video", "PARTHASH", partHash, "JSON", jsonPkg);

    if (!isFullVideo){
        inputs[8] = (char*)pStart;
        inputs[10] = (char*)pEnd;
    }
    nlohmann::json j;
    auto inputStream = ctx->NewStream();
    std::string outputStream;
    if (output){
        outputStream = (*output)["stream_id"].get<string>();
    }else{
        outputStream = ctx->NewStream();
    }
    j["input_hash"] = partHash;
    j["output_stream_id"] = outputStream;
    j["output_media_segment"] = "";

    std::string in_files = j.dump();
    auto ffmpegRet = ctx->FFMPEGRunVideo(inputs, isFullVideo ? szFullParams : szSegmentParams, in_files);
    if (ffmpegRet.second.IsError()){
        // ERROR
        return ffmpegRet;
    }
    std::vector<unsigned char> segData;
    auto loadRet = elv_media_fns::load_all_data_from_stream(ctx, outputStream.c_str(), segData);

    if (loadRet.second.IsError()){
        return ctx->make_error("make_video LoadFromStream", loadRet.second);
    }

    if (output == nullptr){
        if (filename == NULL)
            ctx->Callback(200, "video/mp4", segData.size());
        else
            ctx->CallbackDisposition(200, "video/mp4", segData.size(), filename);
        auto ret = ctx->WriteOutput(segData);
        ctx->CloseStream(outputStream);
        ctx->CloseStream(inputStream);
    }else{ // called by tagger
        ctx->CloseStream(inputStream);
    }

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
elv_return_type
taggit(BitCodeCallContext* ctx, double durationInSecs)
{
    //run --rm --mount src=/home/jan/boatramp,target=/data,type=bind video_tagging:latest boatramp_180s.mov

    char* inputs[] = {
        (char*)"run",
        (char*)"--rm",
        (char*)"--mount",
        (char*)"%INPUTSTREAM%",
        (char*)"video_tagging:latest",
        (char*)"%OUTPUTSTREAM%"
    };

    double roundedUp = ceil(durationInSecs);
    int iRoundedUp = (int)roundedUp;
    int segmentSize = 30;

    auto ret = ctx->SQMDSetJSON((char*)"video_tags", (char*)"{}");
    if (ret.second.IsError()){
        const char* msg = "set video_tags on meta";
        return ctx->make_error(msg, E(msg).Kind(E::Permission));
    }

    for (int segment=0; segment<iRoundedUp; segment+=segmentSize){
        using defer = shared_ptr<void>;
        auto inStream = ctx->NewFileStream();
        auto outStream = ctx->NewFileStream();
        defer _(nullptr, [ctx, inStream, outStream](...){
            LOG_DEBUG(ctx, "Lambda Firing:");
            ctx->CloseStream(inStream["stream_id"]);
            ctx->CloseStream(outStream["stream_id"]);
        });

        nlohmann::json j;
        j["input_stream_id"] = inStream["stream_id"];
        j["output_stream_id"] = outStream["stream_id"];

        char start[64];
        char end[64];
        sprintf(start, "%i", segment);
        int iEnd = (segment + segmentSize) > iRoundedUp ? iRoundedUp : (segment + segmentSize);
        sprintf(end, "%i", iEnd);

        LOG_DEBUG(ctx, "New File",  "stream_id", inStream["stream_id"], "begin_seg", start);

        auto res = make_video(ctx,start,end, &inStream);
        if (res.second.IsError()){
            LOG_ERROR(ctx, "make_video failed", "segment", start);
            continue;
        }
        auto inFiles = j.dump();
        auto tagRet = ctx->TaggerRun(inputs, sizeof(inputs)/sizeof(char*), inFiles);

        if (tagRet.second.IsError()){
            // ERROR
            return tagRet;
        }

        std::vector<unsigned char> segData;
        auto loadRet = elv_media_fns::load_all_data_from_stream(ctx, outStream["stream_id"].get<std::string>().c_str(), segData);

        if (loadRet.second.IsError()){
            return ctx->make_error("make_video LoadFromStream", loadRet.second);
        }

        if (strcmp((const char*)segData.data(),(char*)"") != 0){
            LOG_DEBUG(ctx, "Stream segment info", "data", (const char*)segData.data());
            auto jsonRep = json::parse(segData.data());

            auto video_tags = jsonRep["videoTags"];

            std::string timeIn = FormatTimecode(segment);
            std::string timeOut = FormatTimecode(iEnd);

            json jsonNew = json::array();
            json jsonObj;
            jsonObj["time_in"] = timeIn;
            jsonObj["time_out"] = timeOut;
            jsonObj["tags"] = video_tags;
            jsonNew.push_back(jsonObj);

            auto ret = ctx->SQMDMergeJSON((char*)"video_tags", (char*)jsonNew.dump().c_str());
            if (ret.second.IsError()){
                LOG_ERROR(ctx, "SQMDMergeJSON", "video_tags", jsonNew.dump().c_str(), "inner_error", ret.second.getJSON());
                return ret;
            }
        }else{
            LOG_ERROR(ctx, "Empty JSON returned");
        }
    }


    nlohmann::json j;
    j["headers"] = "application/json";
    j["body"] = "SUCCESS";
    return std::make_pair(j,E(false));
}

/*
 * Externally callable function to invoke the ML video tagging
 *
 * Arguments:
 *      None (all auto)
 * Example URLs:
 *   http://localhost:8008/qlibs/ilibXXX/q/hq__XXX/call/tagger
 */
elv_return_type tagger(BitCodeCallContext* ctx, JPCParams&)
{
    return taggit(ctx, 100.0);
}


/*
    find_ads(BitcodeCallCtx, std::vector<std::string>& tags)

    tags   - vector of strings with tags of interest
*/
elv_return_type find_ads(BitCodeCallContext* ctx, std::vector<std::string>& tags){

    auto kvPkg = ctx->KVGet((char*)"assets");
    if (kvPkg == ""){
        const char* msg = "getting assets from content";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    auto jsonPkg = json::parse(kvPkg);
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
    auto returnFormat = R"({"price":%.2f,"tag":"%s"})";
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
elv_return_type  ad(BitCodeCallContext* ctx, JPCParams& p)
{
    /* Extract an ad library from the list */
    auto adlibid = ctx->KVGet((char *)"eluv.sponsor.qlibid");

    if (adlibid == "") {
        const char* msg = "Failed to retrieve ad library";
        return ctx->make_error(msg, E(msg).Kind(E::IO));
    }
    auto adhash = ctx->KVGet((char *)"eluv.sponsor.qhash");
    if (adhash == "") {
        const char* msg = "Getting eluv.sponsor.qhash";
        return ctx->make_error(msg, E(msg).Kind(E::IO));
    }

    char *headers = (char *)"text/html";

    char body[1024];
    snprintf(body, sizeof(body), (char *)"{\"library\":\"%s\",\"hash\":\"%s\"}", adlibid.c_str(), adhash.c_str());

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    j["body"] = body;
    return std::make_pair(j,0);
}

elv_return_type make_hls_playlist( BitCodeCallContext* ctx, char* req ) {

    // Example requests: /hls/en-master.m3u8
    // hls/en-video-1080p@5120000.m3u8
    // hls/en-master.m3u8
    // keys: ["en", "master.m3u8"], ["en", "video-1080p@5120000.m3u8"]

    int req_len = strlen(req);

    if (req_len == 0){
        std::string msg = "make_hls_playlist bad request len = 0";
        return ctx->make_error(msg, E(msg).Cause(eluvio_errors::ErrorKinds::BadHttpParams));
    }
    char suffix[5];
    char* preamble = (char*)alloca(req_len);
    char language[10];
    char format[20];
    char bandwidth[10];
    char filename[10];

    json j;

    language[0] = 0;
    for (int i=0; i < req_len; i++){
        char curCh = req[i];
        if (curCh == '-' || curCh == '/' || curCh == '.' || curCh == '@') // Not found lang yet
            req[i] = ' ';
    }
    if ( strstr(req, "master") !=NULL ) {
        auto ret = sscanf(req, "%s%s%s%s", preamble, language, filename, suffix);
        if (ret != 4){
            std::string msg = "make_hls_playlist sscanf incorrect params";
            return ctx->make_error(msg, E(msg, std::string("count"), ret));
        }
        LOG_DEBUG( ctx, "request", "pre", preamble, "lang", language, "file", filename, "sfx", suffix );
    } else {
        auto ret = sscanf(req, "%s%s%s%s%s%s", preamble, language, filename, format, bandwidth, suffix);
        if (ret != 6){
            std::string msg = "make_hls_playlist sscanf incorrect params";
            return ctx->make_error(msg, E(msg, std::string("count"), ret));
        }
        LOG_DEBUG( ctx, "request", "pre", preamble, "lang", language, "file", filename, "fmt", format, "bw", bandwidth, "sfx", suffix );
    }

    char offering_keybuf[128];
    sprintf(offering_keybuf, "legOffering.%s", language);

    auto offering_b64_res = ctx->SQMDGetJSON((char*)offering_keybuf);
    if (offering_b64_res.second.IsError()){
        return ctx->make_error("make_hls_playlist", offering_b64_res.second);
    }
    std::string offering_b64 = offering_b64_res.first;

    const char* msg = "make_hls_playlist extract offering";

    if (offering_b64 == ""){
        return ctx->make_error(msg, E(msg, "op","SQMDGetJSON", "offering", (const char*)offering_keybuf));
    }

    LOG_DEBUG(ctx, msg, "key", offering_keybuf, "b64", offering_b64);

    std::string offering_json = base64_decode(offering_b64);

    LOG_DEBUG(ctx, msg, "key", offering_keybuf, "json", offering_json);

    json j2 = json::parse(offering_json);

    char playlist_keybuf[128];

    if (strcmp(filename, "master")==0) {
        sprintf(playlist_keybuf, "%s.%s", filename, suffix);
    } else {
        sprintf(playlist_keybuf, "%s-%s@%s.%s", filename, format, bandwidth, suffix);
    }

    char buf[128];
    sprintf(buf, "offering.%s.hls", language);
    LOG_DEBUG(ctx, "get playlist", "key", buf);
    std::string playlist_b64 = j2["offering"]["hls"][playlist_keybuf];
    LOG_DEBUG(ctx, "encoded playlist", "b64", playlist_b64);

    std::string playlist = base64_decode(playlist_b64);
    LOG_DEBUG(ctx, "decoded playlist", "playlist", playlist);
    ctx->Callback(200, "application/x-mpegURL", playlist.length());

    std::vector<unsigned char> playlistData(playlist.c_str(), playlist.c_str() + playlist.length());

    auto ret = ctx->WriteOutput(playlistData);

    return ctx->make_success();

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
elv_return_type content(BitCodeCallContext* ctx,  JPCParams& p)
{
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }
    auto params = ctx->QueryParams(p);
    if (params.second.IsError()){
        return ctx->make_error("Query Parameters from JSON", params.second);
    }


    typedef enum eMKTypes { VIDEO , AUDIO, MANIFEST, HTML, AD } MKTypes;

    const char* startTag = "start";
    const char* endTag = "end";

    char* pContentRequest = (char*)path.first.c_str();

    LOG_INFO(ctx, "content", "request", pContentRequest);
    char szMK[5];
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    strcpy(szMK, (pContentRequest+cbContentRequest-3));

    /* Fist check for matching URL paths */
    if (strcmp(pContentRequest, "/image") == 0) {
        LOG_DEBUG(ctx, "Performing REP /image");
        return make_image(ctx);
    }
    if (strncmp(pContentRequest, "/checksum", strlen("/checksum")) == 0){
        return elv_media_fns::make_sum(ctx, p);
    }

    auto isDownload = strncmp(pContentRequest, "/download", strlen("/download")) == 0;
    auto isVideo = strncmp(pContentRequest, "/video", strlen("/video")) == 0;
    if (isDownload || isVideo) {

        auto it_start = params.first.find(startTag);
        auto it_end = params.first.find(endTag);
        if (it_end == params.first.end() || it_start == params.first.end()){
            return (isDownload) ? make_video(ctx, NULL,NULL,0, base_download_file) : make_video(ctx, NULL,NULL);
        }

        std::map<std::string, std::string> timeframe;
        timeframe[it_start->first] = it_start->second;
        timeframe[it_end->first] = it_end->second;
        auto startTime = std::stod(timeframe[startTag]);
        auto endTime = std::stod(timeframe[endTag]);
        if (startTime > endTime){
            const char* msg = "Invalid range specified, end must be greater than start";
            LOG_ERROR(ctx, msg, "start," ,startTime, "end",  endTime);
            return ctx->make_error(msg, E(msg).Kind(E::Invalid));
        }
        return (isDownload) ? make_video(ctx, timeframe[startTag].c_str(), timeframe[endTag].c_str(), 0, base_download_file): make_video(ctx, timeframe[startTag].c_str(), timeframe[endTag].c_str());
    }
    if (strcmp(pContentRequest, "/ads") == 0) {
        auto it_tags = params.first.find("tags");
        if (it_tags == params.first.end()){
            const char* msg = "Avmaster no tags provided";
            return ctx->make_error(msg, E(msg).Kind(E::IO));
        }
        std::istringstream iss(it_tags->second);
        std::vector<std::string> tags((std::istream_iterator<WordDelimitedBy<','>>(iss)),
                                        std::istream_iterator<WordDelimitedBy<','>>());
        return find_ads(ctx, tags);
    }

    if (strstr(pContentRequest, "/hls/")) {
        if (strstr(pContentRequest, ".m3u8")) {
            LOG_DEBUG(ctx, "REP /hls playlist", "request url", pContentRequest);
            try {
                return make_hls_playlist(ctx, pContentRequest);
            } catch (std::exception e) {
                return ctx->make_error("make_hls_playlist exception", E(e.what()).Kind(E::Other));
            }
        }
        /* Fall through to serve segments - HLS and DASH segments are the same */
    }

    /* Check for DASH extensions */
    char *dot = strrchr(pContentRequest, '.');
    if (!dot){
        const char* msg = "dash request made but not '.' can be found in URL to parse";
        return ctx->make_error(msg , E(msg).Kind(E::Invalid));
    }

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
    else{
        const char* msg = "Unknown parse type detected";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
    }

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

    LOG_DEBUG(ctx, "content",  "request", pContentRequest,
        "preamble", szPreamble, "lang", szLanguage, "format", szFormat, "bw", bandwidth, "track", track, "mk", szMK);
    char buf[128];
    sprintf(buf, "legOffering.%s", szLanguage);

    auto kvGotten = ctx->SQMDGetJSON((char*)buf);
    auto kvWatermark = ctx->SQMDGetJSON((char*)"watermark");

    if (kvGotten.second.IsError()){
        const char* msg = "json not found";
        return ctx->make_error(msg, kvGotten.second);
    }
    std::string watermark_json = kvWatermark.second.IsError() ? std::string("{}") : kvWatermark.first.dump();
    std::string decoded_json = base64_decode(kvGotten.first.dump().substr(1));

    switch(mk){
        case AUDIO:
        case VIDEO:
        {
            std::string rep_name = std::string(szFormat) + "@" + std::string(bandwidth);
            Dasher dasher((mk==AUDIO)? 'a' : 'v', std::string(szLanguage), rep_name, std::string(track), decoded_json, ctx, watermark_json);
            dasher.initialize();
            DashSegmentGenerator<Dasher> gen(dasher);
            return gen.dashSegment(ctx);

        }
        break;
        case MANIFEST:
            {
                std::string strAuth;
                j = json::parse(decoded_json);
                DashManifest dashManifest(j);
                if (params.first.find("authorization") != params.first.end()){
                    strAuth = params.first["authorization"];
                    LOG_DEBUG(ctx, "Found authorization info");
                }
                std::string manifestString = dashManifest.Create(strAuth);
                ctx->Callback(200, "application/dash+xml",  manifestString.length());
                std::vector<unsigned char> manifestData(manifestString.c_str(), manifestString.c_str()+manifestString.length());
                auto ret = ctx->WriteOutput(manifestData);

                return ctx->make_success();

            }
            break;
    case HTML:
        {
            //return dashHtml(ctx, url);
            const char* msg ="Feature not currently working";
            return ctx->make_error(msg, E(msg).Kind(E::NotImplemented));
        }
    case AD:
        return ad(ctx, p);
    default:
        {
            const char* msg = "unknown url file extension requested";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
    };

    return ctx->make_error("unknown type requested", E("unknown type requested").Kind(E::NotExist));

}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ad)
    MODULE_MAP_ENTRY(tagger)
END_MODULE_MAP()
