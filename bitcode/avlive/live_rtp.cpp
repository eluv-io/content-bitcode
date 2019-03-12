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
#include <thread>
#include <nlohmann/json.hpp>

#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/media.h"

using namespace elv_context;

// Global strings
const char* segment_init = "segment.init";
const char* stream_handle = "stream.handle";

// Encoding parameters
const char *encparam_analyze_duration = "10000000";
const char *encparam_probesize = "2560000";
const char *encparam_segment_duration = "10000000";
const char *encparam_bw = "200k";
const char *encparam_log_level = "error"; /* Setting to debug will cause stderr buffer to fill */

typedef enum eMKTypes { VIDEO , AUDIO, MANIFEST, HLS, TEST } MKTypes;

typedef struct tagFormatTriplet {
    char* Height;
    char* Width;
    char* Bandwidth;
} FormatTriplet;


elv_return_type setObject(BitCodeCallContext* ctx, JPCParams& p){
    auto params = ctx->QueryParams(p);
    if (params.second.IsError()){
        return ctx->make_error("Query Parameters", params.second);
    }

    auto it_video_width = params.first.find("vidformats.width");
    if (it_video_width == params.first.end()){
        return ctx->make_error("vidformats.width", E("Formats width not provided"));
    }

    auto it_video_height = params.first.find("vidformats.height");
    if (it_video_height == params.first.end()){
        return ctx->make_error("vidformats.height", E("Formats height not provided"));
    }
    auto it_video_bw = params.first.find("vidformats.bw");
    if (it_video_bw == params.first.end()){
        return ctx->make_error("vidformats.bw", E("Formats height not provided"));
    }

    auto it_encoder_bw = params.first.find("bandwidth");
    if (it_encoder_bw == params.first.end()){
        LOG_INFO(ctx, "encoder bandwidth not provided defaulting");
    }else{
        ctx->KVSet(it_encoder_bw->first.c_str(),it_encoder_bw->second.c_str());
    }
    auto it_encoder_probesize = params.first.find("probesize");
    if (it_encoder_probesize == params.first.end()){
        LOG_INFO(ctx, "encoder probesize not provided defaulting");
    }else{
        ctx->KVSet(it_encoder_probesize->first.c_str(),it_encoder_probesize->second.c_str());
    }
    auto it_encoder_codec = params.first.find("codec");
    if (it_encoder_codec == params.first.end()){
        LOG_INFO(ctx, "encoder codec not provided defaulting");
    }else{
        ctx->KVSet(it_encoder_codec->first.c_str(),it_encoder_codec->second.c_str());
    }
    auto it_encoder_analyzeduration = params.first.find("analyzeduration");
    if (it_encoder_analyzeduration == params.first.end()){
        LOG_INFO(ctx, "encoder analyzeduration not provided defaulting");
    }else{
        ctx->KVSet(it_encoder_analyzeduration->first.c_str(),it_encoder_analyzeduration->second.c_str());
    }
    auto it_encoder_segmentduration = params.first.find("segmentduration");
    if (it_encoder_segmentduration == params.first.end()){
        LOG_INFO(ctx, "encoder segmentduration not provided defaulting");
    }else{
        ctx->KVSet(it_encoder_segmentduration->first.c_str(),it_encoder_segmentduration->second.c_str());
    }
    auto it_loglevel = params.first.find("loglevel");
    if (it_loglevel == params.first.end()){
        LOG_INFO(ctx, "encoder loglevel not provided defaulting");
    }else{
        ctx->KVSet(it_loglevel->first.c_str(),it_loglevel->second.c_str());
    }



    ctx->KVSet(it_video_width->first.c_str(),it_video_width->second.c_str());
    ctx->KVSet(it_video_height->first.c_str(),it_video_height->second.c_str());
    ctx->KVSet(it_video_bw->first.c_str(),it_video_bw->second.c_str());

    return ctx->make_success();
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
elv_return_type stop_stream(BitCodeCallContext* ctx, JPCParams& p){
    auto qp = ctx->QueryParams(p);
    std::string shandle;
    if (qp.second.IsError() || qp.first.find("handle") == qp.first.end()){
        auto vhandle = ctx->KVGet(stream_handle);
        if (vhandle == "") {
            const char* msg = "getting stream handle";
            return ctx->make_error(msg, E(msg, "stream_handle", stream_handle));
        }
        shandle = vhandle;
    }else{
        shandle = qp.first["handle"];
    }


    // Must remove process handle temp key
    ctx->FFMPEGStopLive(shandle);
    return ctx->make_success();
}

bool LoadDataFromStream(BitCodeCallContext* ctx, const char* stream_name, std::vector<uint8_t>& total){
    auto ret = elv_media_fns::load_all_data_from_stream(ctx, stream_name, total);
    return !ret.second.IsError();
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
elv_return_type dashSegment(BitCodeCallContext* ctx, FormatTriplet* format, char* segName){
    const char* combined[] = {
        "-hide_banner",
        "-nostats",
        "-loglevel", encparam_log_level,
        "-y",
        "-i", "REPLACEME",
        "-copyts",
        "-map", "0:v",
        "-vcodec","%CODEC%",
        "-b:v", "%s",
        "-vf","scale=%s:%s",
        "-force_key_frames", "expr:gte(t,n_forced*2)",
        "-f", "dash",
        "-min_seg_duration",encparam_segment_duration,
        "-use_timeline","1",
        "-use_template","1",
        "-remove_at_exit","0",
        "-init_seg_name","%INIT%",
        "-media_seg_name", "%MEDIA%",
        "%MANIFEST%"
    };

    char segmentName[256];
    strcpy(segmentName, segName);

    int isInit = strcmp(segmentName, "init") == 0;
    int isVideo = (format->Height != 0 && format->Width != 0);
    const int keysz = 1024;
    char keybuf[keysz];
    char segment_init[128];

    sprintf(segment_init, (char*)"live-%s-init", isVideo ? (char*)"video" : (char*)"audio");
    auto parthash = ctx->KVGet(segment_init);
    if (parthash == ""){
        const char* msg = "Failed to get init segment";
        LOG_ERROR(ctx, msg, "segment", segment_init);
        return ctx->make_error(msg,E(msg).Kind(E::NotExist));
    }
    uint32_t size=0, sizeFirst=0;
    auto ret = ctx->QReadPart(parthash.c_str(), 0, -1, &size);
    if (ret->size() == 0){
        const char* msg = "Failed to read Init from part";
        LOG_ERROR(ctx,msg, "parthash", parthash);
        return ctx->make_error(msg,E(msg).Kind(E::NotExist));
    }

    if (isInit){
        auto kvRet = ctx->KVGet("manifest.dash");
        if (kvRet == ""){
            const char* msg = "could not find manifest in fabric";
            return ctx->make_error(msg,E(msg).Kind(E::NotExist));
        }

        const char* segmentTimeline = strstr(kvRet.c_str(), "<SegmentTimeline>");
        if (segmentTimeline == 0){
            const char* msg = "could not find <SegmentTimeline> in manifest";
            return ctx->make_error(msg,E(msg));
        }
        const char* expr = (char*)"<S t=\"";
        const char* tequals = strstr(segmentTimeline, expr);
        if (tequals == 0){
            const char* msg = "could not find <S t=\" in manifest";
            return ctx->make_error(msg,E(msg));
        }
        //char segmentName[32];
        tequals += strlen(expr);
        const char* segend = strstr(tequals, "\"");
        if (segend == 0){
            return ctx->make_error("could not find end quote in Sgement",E("dashSegement", "curParse", tequals));
        }
        strncpy(segmentName, tequals, segend-tequals);
        segmentName[segend-tequals] = 0;
    }
    // ret has init segment must write file when all success
    // get the mpd for 1st segement name
    snprintf(keybuf, keysz, "live-%s-%s", isVideo ? "video" : "audio", segmentName);
    auto firstPartHash = ctx->KVGet(keybuf);
    if (firstPartHash == ""){
        const char* msg = "KVGet could not find live key";
        LOG_ERROR(ctx, msg, "key", keybuf);
        return ctx->make_error(msg,E(msg));
    }
    auto retFirst = ctx->QReadPart(firstPartHash.c_str(), 0, -1, &sizeFirst);
    if (retFirst->size() == 0){
        const char* msg = "QReadPart failed";
        LOG_ERROR(ctx, msg, "segmentName", segmentName,  "parthash", parthash);
        return ctx->make_error(msg,E(msg));
    }

    auto concat = ctx->NewFileStream();
    auto concat_stream = concat["stream_id"].get<string>();
    auto concat_filename = concat["file_name"].get<string>();

    auto out_stream = ctx->NewStream();
    // these need to be merged and written
    std::vector<std::uint8_t> segDataOrig(size+sizeFirst);
    memcpy(segDataOrig.data(), ret->data(), size);
    memcpy(segDataOrig.data()+size, retFirst->data(), sizeFirst);
    auto r = ctx->WriteStream(concat_stream, segDataOrig);
    if (r.second.IsError()){
        const char* msg = "WriteStream failed";
        LOG_ERROR(ctx, msg, r.first["error"]);
        return ctx->make_error(r.first["error"], r.second);
    }
    combined[6] = concat_filename.c_str();

    char newBValue[1024];
    char newScaleValues[1024];

    sprintf(newBValue, combined[13], format->Bandwidth);
    combined[13] = newBValue;
    sprintf(newScaleValues, combined[15], format->Width, format->Height);
    combined[15] = newScaleValues;

    nlohmann::json j;
    j["output_manifest"] = "";
	j["input_count"] = "0";
    j["output_init_segment"] = isInit ? "init-seg" : "";
    j["output_media_segment"]= "";
    j["output_manifest"] = "";
    j["output_stream_id"] = out_stream;
    j["is_init"] = isInit == 0 ? false : true;

    std::string inputs = j.dump();
    auto ffmpeg_ret = ctx->FFMPEGRunEx((char**)combined, sizeof(combined)/sizeof(char*), inputs);
    ctx->CloseStream(concat["stream_id"]);

    std::vector<unsigned char> total;
    if (!LoadDataFromStream(ctx, out_stream.c_str(), total)){
        const char* msg = "dashSegement LoadDataFromStream from file";
        return ctx->make_error(msg, E(msg, "stream", out_stream));
    }
    ctx->CloseStream(out_stream);

    if (ffmpeg_ret.second.IsError()){
        return ffmpeg_ret;
    }

    // Get Data
    // CloseStream on files

    if (!isInit){
        LOG_INFO(ctx, "Calling FixupLive");
        /* Fix ffmpeg segment sequence and base time in place */
        auto res = FFmpegFixup::ffmpegFixupLive(ctx, (char*)total.data(), total.size(), (char*)segDataOrig.data(), segDataOrig.size());
        if (res < 0) {
            const char* msg = "fixing segment";
            return ctx->make_error(msg, E(msg, "outstream", out_stream));
        }
    }
    std::string media_type = (isVideo) ? "video/mp4" : "audio/mp4";
    ctx->Callback(200, media_type.c_str(),total.size());
    ctx->WriteOutput(total);
    return ctx->make_success();
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
elv_return_type dashSegmentRaw(BitCodeCallContext* ctx, char *segment, int isVideo ){
    const int keysz = 1024;
    char keybuf[keysz];
    snprintf(keybuf, keysz, "live-%s-%s",(isVideo ? "0" : "1"), segment);
    auto parthash = ctx->KVGet(keybuf);
    if (parthash != ""){
        uint32_t size=0;
        auto ret = ctx->QReadPart(parthash.c_str(), 0, -1, &size);
        if (ret->size() != 0){
            std::string media_type = (isVideo) ? "video/mp4" : "audio/mp4";
            ctx->Callback(200, media_type.c_str(), ret->size());
            ctx->WriteOutput(*ret);
            return ctx->make_success();
        }
        else{
            const char* msg = "No Temp part found";
            LOG_ERROR(ctx, msg, "hash", parthash.c_str());
            return ctx->make_error(msg, E(msg).Kind(E::IO));
        }
    }
    const char* msg = "part not found for segment";
    LOG_ERROR(ctx, msg, "segment", segment);
    return ctx->make_error(msg, E(msg).Kind(E::IO));
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
elv_return_type dashManifestRaw(BitCodeCallContext* ctx)
{
    auto kvRet = ctx->KVGet("manifest.dash");
    if (kvRet == ""){
        const char* msg = "Error could not find manifest in fabric";
        return ctx->make_error(msg,E(msg).Kind(E::NotExist));
    }
    std::vector<uint8_t> vec(kvRet.c_str(), kvRet.c_str() + kvRet.length());

    ctx->Callback(200, "application/dash+xml", vec.size());
    ctx->WriteOutput(vec);
    return ctx->make_success();
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
char* expandNextProperty(BitCodeCallContext* ctx, const char* bufbegin, char* tag, char* pbuf, const char* expand){
    char* bwpos = strstr((char*)bufbegin, tag);
    if (bwpos == 0){
        LOG_ERROR(ctx, "Failed to find begin for tag=", tag);
        return 0;
    }
    strncpy(pbuf, bufbegin, bwpos-bufbegin); // up to tag
    pbuf += bwpos-bufbegin;
    strcpy(pbuf, tag);
    pbuf += strlen(tag);
    strcpy(pbuf, expand);
    char* end =  strstr(bwpos, "\"") + 1; //other end of tag
    if (end == 0) {
        LOG_ERROR(ctx, "didn't find end string");
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
void replaceFilenameTerm(BitCodeCallContext* ctx, char* begin, char ch){
    const char* ext = ".m4s";
    for (int j = 0;j < 2; j++){
        char* sToV = strstr(begin, ext);
        if (sToV == 0){
            LOG_ERROR(ctx, "Could not find ext=",ext, "in=",begin);
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
elv_return_type dashManifest(BitCodeCallContext* ctx){

    auto kvRet = ctx->KVGet("manifest.dash");
    if (kvRet == ""){
        const char* msg = "Error could not find manifest in fabric";
        return ctx->make_error(msg,E(msg).Kind(E::NotExist));
    }
    LOG_DEBUG(ctx, "Dash Manifest", "manifest", kvRet);
    int cbManifest = kvRet.length();

    const int manifestExtra = 16384; // extra space to allocate to modify manifest
    auto return_buffer = CHAR_BASED_AUTO_RELEASE((char*)malloc(cbManifest + manifestExtra));
    char* pbuf = return_buffer.get(); // current buffer pointer
    if (pbuf == nullptr){
        const char* msg = "failed to allocate manifest buffer";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
    }

    // These variables maintain the video configuration (width x height x bandwidth)
    int nbwelems = 1;

    auto width_elems = ctx->KVGet((char*)"vidformats.width");
    if (width_elems == ""){
        const char* msg = "Getting vidformats.width";
        return ctx->make_error(msg,E(msg).Kind(E::BadHttpParams));
    }
    //if (width_elems.get() == NULL) width_elems = strdup("");

    auto height_elems = ctx->KVGet((char*)"vidformats.height");
    if (height_elems == ""){
        const char* msg = "Getting vidformats.height";
        return ctx->make_error(msg,E(msg).Kind(E::BadHttpParams));
    }

    auto bw_elems = ctx->KVGet((char*)"vidformats.bw");
    if (bw_elems == ""){
        const char* msg = "Getting vidformats.bw";
        return ctx->make_error(msg,E(msg).Kind(E::BadHttpParams));
    }

    /*
    We are about to look for the first tag of interest

	<Period id="0" start="PT0.0S">
		<AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true">

    */
    const char* pAdaptation = strstr(kvRet.c_str(), "<AdaptationSet");
    if (pAdaptation == 0){
        const char* msg = "Getting AdaptionSet begin";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    const char* endAdapt = strstr(pAdaptation, ">"); // found our AdaptationSet
    if (endAdapt == 0){
        const char* msg = "Getting AdaptionSet end";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
    }
    // copy <AdaptationSet id="0" contentType="video" segmentAlignment="true" bitstreamSwitching="true"> to output
    strncpy(pbuf, kvRet.c_str(), (endAdapt+1)-kvRet.c_str());
    pbuf += (endAdapt+1)-kvRet.c_str(); // move insert head

    char* endVideo = 0; //End of the video section in manifest

    const char* repbeginTag = "<Representation id=\""; //tag for begin rep

    // The following loop handles each Representation (transcode config)
    // Currently we have 2 supported resolutions but this loop
    // accomodates any number
    for (int i=0; i<nbwelems; i++){
    // find our first begin replacement
        char triplet[2048];
        sprintf(triplet, "%sx%s-%s", width_elems.c_str(), height_elems.c_str(), bw_elems.c_str());

        const char* repbegin = strstr(endAdapt, repbeginTag);
        if (repbegin == 0){
            const char* msg = "Getting Representation 0";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
        strncpy(pbuf, repbegin, strlen(repbeginTag)); //buffer has up to '<Representation id="'
        pbuf += strlen(repbeginTag);
        strcpy(pbuf, triplet); // put triplet
        pbuf += strlen(triplet); // skip past
        repbegin += strlen(repbeginTag) + 1; //skip past 0/1

        // This are we are stitching together the original xml with the
        // original Rep Id=X --> RepId=WxHxBW

        char* bwexpand = expandNextProperty(ctx, repbegin, (char*)"bandwidth=\"", pbuf, bw_elems.c_str());
        if (bwexpand == 0){
            const char* msg = "Getting bandwidth";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
        pbuf += bwexpand-repbegin; //move insert head
        char* widthexp = expandNextProperty(ctx, bwexpand, (char*)"width=\"",pbuf, width_elems.c_str());
        if (widthexp == 0){
            const char* msg = "Getting  width";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
        pbuf += widthexp-bwexpand; //move insert head
        char* heightexp= expandNextProperty(ctx, widthexp, (char*)"height=\"",pbuf, height_elems.c_str());
        if (heightexp == 0){
            const char* msg = "Getting height";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
        pbuf += heightexp-widthexp;  //move insert head

        // Move us to the end of this Rep section
        char* endrep = (char*)"</Representation>";

        char* end = strstr(heightexp, endrep);
        if (end == 0){
            const char* msg = "Getting </Representation>";
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }
        end += strlen(endrep);

        // Move the remainder of the Rep section out to the return buffer
        char* pbufAtHeight = pbuf;
        strncpy(pbuf, heightexp, (end-heightexp));
        pbuf += end-heightexp;
        endVideo = end;
        // replace the m4s to m4v in the section (and forward) we just expanded
        replaceFilenameTerm(ctx, pbufAtHeight, 'v');

    }


    // Copy the remainder of the characters from end rep to audio rep as they
    // remain unchanged
    char* audioRep = strstr(endVideo, repbeginTag);
    strncpy(pbuf, endVideo, audioRep-endVideo);
    pbuf += audioRep-endVideo;//move insert head

    // Handle audio need to replace <Representation id="1" with <Representation id="STEREO-128000"
    char* audioTag =  (char*)"STEREO-128000";
    if (audioRep == 0){
        const char* msg = "Getting audio section";
        return ctx->make_error(msg, E(msg).Kind(E::NotExist));
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
    replaceFilenameTerm(ctx, pbuf, 'a');
    std::vector<uint8_t> vec(return_buffer.get(), return_buffer.get() + strlen(return_buffer.get()));
    ctx->Callback(200, "application/dash+xml", vec.size());
    auto ret = ctx->WriteOutput(vec);

    return ctx->make_success();

}

elv_return_type make_hls_playlist( BitCodeCallContext* ctx, char* req ) {

    // Example requests: /hls/master.m3u8
    // hls/video-width@height@bw.m3u8
    // hls/en-master.m3u8
    // keys: ["en", "master.m3u8"], ["en", "video-1080p@5120000.m3u8"]

    int req_len = strlen(req);

    if (req_len == 0){
        std::string msg = "make_hls_playlist bad request len = 0";
        return ctx->make_error(msg, E(msg).Cause(eluvio_errors::ErrorKinds::BadHttpParams));
    }
    char* suffix    = (char*)alloca(req_len);
    char* preamble  = (char*)alloca(req_len);
    char* format    = (char*)alloca(req_len);
    char* bandwidth = (char*)alloca(req_len);
    char* language  = (char*)alloca(req_len);
    char* filename  = (char*)alloca(req_len);

    if (!suffix || !preamble || !format || !bandwidth || !language || !filename){
        const char* msg = "make_hls_playlist alloca failed";
        return ctx->make_error(msg, E(msg));
    }

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
    } else if (strstr(req, "audio") !=NULL ){
        auto ret = sscanf(req, "%s%s%s%s%s", preamble, language, filename, bandwidth, suffix);
        if (ret != 5){
            std::string msg = "make_hls_playlist sscanf incorrect params";
            return ctx->make_error(msg, E(msg, std::string("count"), ret));
        }
        LOG_DEBUG( ctx, "request", "pre", preamble, "lang", language, "file", filename, "bw", bandwidth, "sfx", suffix );
    }
    else{
        auto ret = sscanf(req, "%s%s%s%s%s%s", preamble, language, filename, format, bandwidth, suffix);
        if (ret != 6){
            std::string msg = "make_hls_playlist sscanf incorrect params";
            return ctx->make_error(msg, E(msg, std::string("count"), ret));
        }
        LOG_DEBUG( ctx, "request", "pre", preamble, "lang", language, "file", filename, "fmt", format, "bw", bandwidth, "sfx", suffix );
    }

    char playlist_keybuf[128];

    if (strcmp(filename, "master")==0) {
        sprintf(playlist_keybuf, "%s", filename);
    } else if (strcmp(filename, "audio") == 0){
        sprintf(playlist_keybuf, "%s-%s@%s", language, filename, bandwidth);
    }else{
        sprintf(playlist_keybuf, "%s-%s-%s@%s", language, filename, format, bandwidth);
    }

    auto playlist = ctx->KVGet(playlist_keybuf);

    ctx->Callback(200, "application/x-mpegURL", playlist.length());

    std::vector<unsigned char> playlistData(playlist.c_str(), playlist.c_str() + playlist.length());

    auto ret = ctx->WriteOutput(playlistData);

    return ctx->make_success();

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
void  loadFromStreamAndSaveData(BitCodeCallContext* ctx, const char* stream_name, bool isVideo, const char* timestamp, const char* qwt, elv_return_type* ret){
    std::vector<uint8_t> segData;
    segData.reserve(10000000);
    elv_return_type elv_null({}, false);
    if (ret == NULL){
        LOG_WARN(ctx, "Calling loadFromStreamAndSaveData with a NULL error handler");
        ret = &elv_null;
    }

    if (!LoadDataFromStream(ctx, stream_name, segData)){
        const char* msg = "LoadDataFromStream from file";
        *ret = ctx->make_error(msg, E(msg).Kind(E::Other));
        return;
    }

    std::string ssqwt = qwt;

    auto parthash = ctx->QCreatePart(ssqwt, segData);
    if (parthash == ""){
        const char* msg = "Getting parthash";
        *ret = ctx->make_error(msg, E(msg).Kind(E::Invalid));
        return;
    }

    auto base = string_format( "live-%s-%s",(isVideo ? "video" : "audio"), timestamp);
    auto baseRaw = string_format( "live-%s-%s",(isVideo ? "0" : "1"), timestamp);
    LOG_DEBUG(ctx, "Part Created", "parthash", parthash.c_str(), "partname", base.c_str());

    auto base_ret = ctx->KVSetMutable(ssqwt.c_str(), base.c_str(), parthash.c_str());
    if (!base_ret){
        const char* msg = "KVSetMutable";
        LOG_ERROR(ctx, msg, "ssqwt", ssqwt.c_str(), "base", base, "parthash", parthash.c_str());
        *ret = ctx->make_error(msg,E(msg));
        return;
    }
    auto baseraw_ret = ctx->KVSetMutable(ssqwt.c_str(), baseRaw.c_str(), parthash.c_str());
    if (!baseraw_ret){
        const char* msg = "KVSetMutable";
        LOG_ERROR(ctx, msg, "ssqwt", ssqwt.c_str(), "baseRaw", baseRaw, "parthash", parthash.c_str());
        *ret = ctx->make_error(msg,E(msg));
        return;
    }
    ctx->CloseStream(stream_name);
    *ret = ctx->make_success();
    return;
}

elv_return_type loadHLS(BitCodeCallContext* ctx, std::string& master_stream, std::vector<std::string>& hlsData, std::vector<std::string>& labels, const char* qwt){
    std::vector<unsigned char> data;
    for (int i =0; i < hlsData.size(); i++){
        data.clear();
        auto read_ret = LoadDataFromStream(ctx,hlsData[i].c_str(), data);
        if (!read_ret){
            return ctx->make_error("LoadDataFromStream failed", E("Read stream", "stream_name", labels[i]));
        }
        if (data.size() > 0){
            LOG_INFO(ctx, "loadHLS writting meta data", "key", labels[i]);
            std::string hls_playlist(data.begin(), data.end());
            ctx->KVSetMutable(qwt, labels[i].c_str(), hls_playlist.c_str());
        }
    }
    if (master_stream != ""){
        data.clear();
        auto read_ret = LoadDataFromStream(ctx,master_stream.c_str(), data);
        if (!read_ret){
            return ctx->make_error("LoadDataFromStream failed", E("Read stream", "stream_name", "master"));
        }
        if (data.size() > 0){
            LOG_INFO(ctx, "loadHLS writting master meta data");
            std::string hls_playlist(data.begin(), data.end());
            ctx->KVSetMutable(qwt, "master", hls_playlist.c_str());
        }
    }

    return ctx->make_success();
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
elv_return_type loadManifestAndSaveData(BitCodeCallContext* ctx, const char* stream, const char* qwt){
    std::vector<unsigned char> manData;
    if (!LoadDataFromStream(ctx, stream, manData)){
        return ctx->make_error("LoadDataFromStream failed", E("Read stream", "stream_name", stream));
    }
    manData.emplace_back(0);
//   This section changes the 2 properties
//   Its purpose is to limit delay between live broadcast and transcode
    char* suggested_delay = (char*)"suggestedPresentationDelay=";
    char* newdelay = (char*)"PT0S";
    char* min_buffer_time = (char*)"minBufferTime=";
    char* newmin = (char*)"PT2.0S";
    char* delay = strstr((char*)manData.data(), suggested_delay);
    delay += strlen(suggested_delay);
    strncpy(delay+1, newdelay, strlen(newdelay));
    char* minbuffer = strstr((char*)manData.data(), min_buffer_time);
    minbuffer += strlen(min_buffer_time);
    strncpy(minbuffer+1, newmin, strlen(newmin));
//  Insert the manifets into the fabric as a KVSet

    std::string ssqwt = qwt;
    bool bSet = ctx->KVSetMutable(ssqwt.c_str(), "manifest.dash", (const char*)manData.data());
    if (!bSet){
        const char* msg = "KVSet Manifest";
        LOG_ERROR(ctx, msg , "ID", ssqwt);
        return ctx->make_error(msg,E(msg).Kind(E::IO));
    }

    ctx->CloseStream(stream);
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
elv_return_type ffmpeg_update_cbk_bulk(BitCodeCallContext* ctx, JPCParams& p){
    auto stream_id = p["stream_id"].get<std::vector<std::string>>();
    auto typ = p["type"].get<std::vector<std::string>>();
    auto timestamp = p["timestamp"].get<std::vector<std::string>>();
    auto manifestData = p["manifest"].get<std::string>();
    auto hlsData = p["hls"].get<std::vector<std::string>>();
    auto labels = p["labels"].get<std::vector<std::string>>();
    auto hlsMaster = (p.find("master_stream") != p.end()) ? p["master_stream"].get<std::string>() : "";
    using defer = shared_ptr<void>;

    LOG_DEBUG(ctx, "ffmpeg_update_cbk_bulk", "params", p.dump());

    auto ssqwt = ctx->QModifyContent("");
    auto stream_size = stream_id.size();
    std::thread threads[stream_size];
    elv_return_type* rets[stream_size];
    elv_return_type** prets = rets;

    //This function will fire at outer function exit ala golang defer
    defer _(nullptr, [stream_size, prets](...){
        for (int i = 0; i < stream_size; i++){
             delete prets[i];
        }
    });

    int pos = 0;
    for (auto& stream : stream_id){
        auto curType = typ[pos].c_str();
        elv_return_type* pret = new elv_return_type(nlohmann::json({}), E(false));
        rets[pos] = pret;
        threads[pos] = std::thread(loadFromStreamAndSaveData,ctx, stream.c_str(), strcmp(curType, "VIDEO") == 0, timestamp[pos].c_str(), ssqwt.c_str(), pret);
        pos++;
    }

    loadManifestAndSaveData(ctx, manifestData.c_str(), ssqwt.c_str());
    loadHLS(ctx, hlsMaster, hlsData, labels, ssqwt.c_str());

    for (auto& th : threads) {
        th.join();
    }

    auto sstoken = ctx->QFinalizeContent(ssqwt);
    if (sstoken.first == ""){
        const char* msg = "QFinalizeContent";
        LOG_ERROR(ctx, msg, "ssqwt", ssqwt.c_str());
        return ctx->make_error(msg,E(msg, "ssqwt", ssqwt.c_str()));
    }else{
        LOG_INFO(ctx, "FinalizeContent", "QID", sstoken.first.c_str(), "QHASH", sstoken.second.c_str());
    }
    auto ret = ctx->QPublishContent(sstoken.second);
    if (ret.second.IsError()){
        const char* msg = "QPublishContent";
        LOG_ERROR(ctx, msg, "ssqhash", sstoken.second.c_str());
        return ctx->make_error(msg,E(msg));
    }

    for (int i = 0; i < stream_id.size(); i++){
        if (rets[i]->second.IsError()){
            LOG_ERROR(ctx, "loadFromStreamAndSaveData:", "timestamp", timestamp[i].c_str());
            return *rets[i];
        }
    }
    return ctx->make_success();
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
elv_return_type processContentRequest(BitCodeCallContext* ctx, char* pContentRequest, MKTypes mk){
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

        auto scanRet = sscanf(pContentRequest, "%s%s%s%s%s%s\n", dash, live, widthheight, bandwidth, segment, ext);

        if (scanRet != 6){
            const char* msg = "bad uri format";
            return ctx->make_error(msg, E(msg).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
        }

        int cbWidthHeight = strlen(widthheight);

        for (int i=0;i<cbWidthHeight;i++){
            if (widthheight[i] == 'x'){
                widthheight[i] = 0;
                height = &widthheight[i+1];
            }
        }
        LOG_INFO(ctx, "contect request parsed", "dash", dash, "live", live,  "width", width,  "height", height,  "bandwidth", bandwidth,  "segment", segment, "ext", ext);

        FormatTriplet format;
        format.Height = height;
        format.Width = width;
        format.Bandwidth = bandwidth;
        return dashSegment(ctx, &format, segment);
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

        auto scanRet = sscanf(pContentRequest, "%s%s%s%s%s%s\n", dash, live, widthheight, bandwidth, segment, ext);

        if (scanRet != 6){
            const char* msg = "bad uri format";
            return ctx->make_error(msg, E(msg).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
        }

        LOG_INFO(ctx, "connect request parsed", "dash", dash, "live", live,  "widthheight", widthheight,  "bandwidth", bandwidth,  "segment", segment, "ext", ext);

        return dashSegmentRaw(ctx,segment, 0);

    }
    break;
    case MANIFEST:
    {
        return dashManifest(ctx);
    }
    break;
    case HLS:
    {
        return make_hls_playlist(ctx, pContentRequest);
    }
    break;
    case TEST:{
        const char* msg = "Test not yet implemented";
        return ctx->make_error(msg, E(msg).Kind(E::NotImplemented));
    }
    };
    const char* msg = "default case hit unexpectedly";
    return ctx->make_error(msg, E(msg).Kind(E::Other));
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
elv_return_type processContentRequestRaw(BitCodeCallContext* ctx, char* pContentRequest, MKTypes mk){

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
        raw[0] = 0;
        for (int i=0; i < cbContentRequest;i++){
            char curCh = pContentRequest[i];
            if (curCh == '-' || curCh == '/' || curCh == '.') // Not found lang yet
                pContentRequest[i] = ' ';
        }

        auto scanRet = sscanf(pContentRequest, " %s %s %s %s %s %s", szPreamble, raw, type, contents, track, ext);
        if (scanRet != 6){
            const char* msg = "bad uri format";
            return ctx->make_error(msg, E(msg).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
        }

        LOG_INFO(ctx, "content request raw parsed", "preamble", szPreamble, "raw", raw,  "type", type, "contents", contents, "track", track, "ext",  ext);

        auto dash_res = dashSegmentRaw(ctx, track,mk==VIDEO?1:0);
        if (dash_res.second.IsError()){
            LOG_ERROR(ctx, "dashSegmentRaw failed");
            return dash_res;
        }
        return dash_res;

    }
    break;
    case MANIFEST:
    {
        return dashManifestRaw(ctx);
    }
    break;
    case HLS:
    {
        return make_hls_playlist(ctx, pContentRequest);
    }
    break;
    case TEST:
        const char* msg = "Test not yet implemented";
        return ctx->make_error(msg, E(msg).Kind(E::NotImplemented));
    };
    const char* msg = "default case hit unexpectedly";
    return ctx->make_error(msg, E(msg).Kind(E::Other));
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
elv_return_type start_stream(BitCodeCallContext* ctx, JPCParams&){
    auto codecParam = ctx->KVGet("codec");
    auto bandwidthParam = ctx->KVGet("bandwidth");
    auto probesizeParam = ctx->KVGet("probesize");
    auto analyzedurationParam = ctx->KVGet("analyzeduration");
    auto segmentdurationParam = ctx->KVGet("segmentduration");
    auto loglevelParam = ctx->KVGet("loglevel");
    LOG_INFO (ctx, "start_stream");

    const char* inputs[] = {
        "-loglevel", loglevelParam == "" ? encparam_log_level : loglevelParam.c_str(),
        "-hide_banner",
        "-nostats",
        "-y",
        "-i","%PORT%",
        "-analyzeduration", analyzedurationParam == "" ? encparam_analyze_duration : analyzedurationParam.c_str(),
        "-probesize", probesizeParam == "" ? encparam_probesize : probesizeParam.c_str(),
        "-fflags", "nobuffer",
        "-avoid_negative_ts","make_zero",
        "-acodec","aac",
        "-b:a","128k",
        "-vcodec", "%CODEC%",
        "-b:v", bandwidthParam == "" ? encparam_bw : bandwidthParam.c_str() ,
        "-vf", "format=yuv420p",
        "-profile:v", "high",
        "-force_key_frames", "expr:gte(t,n_forced*2)",
        "-f", "dash",
        "-adaptation_sets", "id=0,streams=v id=1,streams=a",
        "-min_seg_duration",segmentdurationParam == "" ? encparam_segment_duration : segmentdurationParam.c_str(),
        "-use_timeline","1",
        "-use_template","1",
        "-remove_at_exit","0",
        "-init_seg_name","live-$RepresentationID$-init.m4s",
        "-media_seg_name", "live-$RepresentationID$-$Time$.m4s",
        "%MANIFEST%"
    };

    auto ffmpeg_return = ctx->FFMPEGRunLive((char**)inputs, sizeof(inputs)/sizeof(char*), "ffmpeg_update_cbk_bulk");
    if (ffmpeg_return.second.IsError()){
        return ffmpeg_return;
    }

    // need to ensure result set is of expected format
    std::string handle = ffmpeg_return.first["handle"].get<string>();
    std::string portstr = ffmpeg_return.first["port"].get<std::string>();
    nlohmann::json j_ret;

    j_ret["handle"] = handle;
    j_ret["port"] = portstr;
    j_ret["headers"] = "application/json";

    std::string html_ret = string_format(R"(Handle=%s, Port=%s)", handle.c_str(), portstr.c_str());
    auto ssqwt = ctx->QModifyContent("");
    if (ssqwt == ""){
        const char* msg = "Modify Content";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
    }
    if (!ctx->KVSetMutable(ssqwt.c_str(), stream_handle, handle.c_str())){
        const char* msg = "KVSetMtuable start_stream";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
    }
    if (!ctx->KVSetMutable(ssqwt.c_str(), "stream.property.port", portstr.c_str())){
        const char* msg = "KVSetMtuable stream.property.port";
        return ctx->make_error(msg, E(msg).Kind(E::Other));
    }
    auto sstoken = ctx->QFinalizeContent(ssqwt);
    if (sstoken.first == ""){
        const char* msg = " QFinalizeContent";
        LOG_ERROR(ctx, msg, "ssqwt", ssqwt.c_str());
        return ctx->make_error(msg,E(msg));
    }else{
        LOG_DEBUG(ctx, "NEW SEGMENT TOKEN", "first", sstoken.first.c_str(), "second", sstoken.second.c_str());
    }
    auto err = ctx->QPublishContent(sstoken.second);
    if (err.second.IsError()){
        const char* msg = "QPublishContent";
        LOG_ERROR(ctx, msg, "ssqhash", sstoken.second.c_str());
        return ctx->make_error(msg,E(msg));
    }

    auto j_str = j_ret.dump();
    ctx->Callback(200, "application/json", j_str.length());

    std::vector<std::uint8_t> jsonData(j_str.c_str(), j_str.c_str()+j_str.length());
    auto ret = ctx->WriteOutput(jsonData);

    return ctx->make_success(j_ret);
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
elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("parsing json params", path.second);
    }

    char* pContentRequest = (char*)path.first.c_str();
    char szMK[5];
    const int isRaw = strstr(pContentRequest, "raw/") != 0;
    int cbContentRequest = strlen(pContentRequest);
    MKTypes mk;
    auto offsetDot = pContentRequest+cbContentRequest -strchr(pContentRequest, '.') - 1;
    strcpy(szMK, (pContentRequest+cbContentRequest-offsetDot));
    if (isRaw){
        if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
            mk = MANIFEST;
        else if (strcmp(szMK, "m4s") == 0){
            mk = (strstr(pContentRequest, "-0-")) ? VIDEO:AUDIO;
        }
        else if (strcmp(szMK, "m4a") == 0)
            mk = AUDIO;
        else if (strcmp(szMK, "m3u8") == 0)
            mk = HLS;
        else
            mk = TEST;
    }
    else{
        if (strcmp(szMK, "mpd") == 0)  // really need to return error if not matching any
            mk = MANIFEST;

        else if (strcmp(szMK, "m4a") == 0)
            mk = AUDIO;
        else if (strcmp(szMK, "m4v") == 0)
            mk = VIDEO;
        else if (strcmp(szMK, "m3u8") == 0)
            mk = HLS;
        else if (strcmp(szMK, "tst") == 0)
            mk = TEST;
        else{
            const char* msg = "Unknown content request for extension";
            LOG_ERROR(ctx, msg, "TYPE", szMK);
            return ctx->make_error(msg, E(msg).Kind(E::NotExist));
        }

    }

    if (isRaw){
        return processContentRequestRaw(ctx, pContentRequest, mk);
    }
    else{
        return processContentRequest(ctx, pContentRequest, mk);
    }
}


BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ffmpeg_update_cbk_bulk)
    MODULE_MAP_ENTRY(setObject)
    MODULE_MAP_ENTRY(start_stream)
    MODULE_MAP_ENTRY(stop_stream)
END_MODULE_MAP()

