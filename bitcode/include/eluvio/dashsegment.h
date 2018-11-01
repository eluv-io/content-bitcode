#pragma once

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
#include <nlohmann/json.hpp>
#include "eluvio/dasher.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/fixup-cpp.h"

using namespace elv_context;
class DashSegmentGenerator{
private:
    Dasher& p;

public:
    DashSegmentGenerator(Dasher& p) : p(p) {}
    std::pair<nlohmann::json,int> dashSegment(BitCodeCallContext* ctx){
        CommandlineGenerator<true> clg2(ctx, p.p);
        std::vector<std::string> v2 = clg2.Flatten();

        int elementCount2 = v2.size();
        char** ppv2 = (char**)malloc(elementCount2*sizeof(char*)); //enough to hold entire vector of strings

        for (int i2=0;i2<elementCount2;i2++){
            ppv2[i2] = (char*)malloc((v2[i2].length()+1)*sizeof(char));
            strcpy(ppv2[i2], v2[i2].c_str());
        }
        std::string strPL = p.p.partListToJSONString();
        LOG_INFO(ctx, "PartList in JSON:",strPL);;

        nlohmann::json j;
        auto inputStream = ctx->NewStream();
        auto outputStream = ctx->NewStream();
        j["input_count"] = std::to_string(p.p.part_list.size());
        for (int iPart=0;iPart<p.p.part_list.size();iPart++){
            auto curInput = string_format("input_hash_%d", iPart) ;
            j[curInput] = p.p.part_list[iPart].phash;

        }
        j["output_init_segment"] = p.p.seg_num == "init" ? inputStream["stream_id"] : "";
        j["output_media_segment"] = p.p.seg_num != "init" ? inputStream["stream_id"] : "";
        j["output_manifest"] = "";
        j["output_stream_id"] = outputStream["stream_id"];

        std::string inputs = j.dump();
        auto ffmpeg_ret = ctx->FFMPEGRunEx(ppv2, elementCount2, inputs);

        if (ffmpeg_ret != 0){
            return ctx->make_error("FFPEGRun Failed", ffmpeg_ret);
        }
        std::string file_to_load = (clg2.IsInit()) ? clg2.GetInitSegmentFilename() : clg2.GetMediaSegmentFilename();
        std::string media_type = (clg2._params.avtype == 'v') ? "video/mp4" : "audio/mp4";
        int chunks_or_frames = (clg2._params.avtype == 'v') ? p.get_video_frames() : p.get_audio_chunks();

        for (int i=0;i<elementCount2;i++){
            free(ppv2[i]);
        }
        free(ppv2);

        int segmentNumber = 0;
        if (p.p.seg_num != "init"){
            segmentNumber = atoi(p.p.seg_num.c_str());
        }
        const int allocSize = 30000000;
        std::vector<unsigned char> segData(allocSize);
        auto read_ret = ctx->ReadStream(outputStream["stream_id"], segData);
        if (read_ret.second != 0){
            ctx->Error("Read stream failed", "{}");
            return read_ret;
        }
        int fsize = read_ret.first["read"];

        if (fsize == allocSize){
            const char* error_msg = "Buffer too small";
            LOG_ERROR(ctx, error_msg);
            return ctx->make_error(error_msg, -11);
        }


        ctx->CloseStream(outputStream["stream_id"]);

        if (segmentNumber != 0){
            LOG_INFO(ctx, "DBG: FFmpegFixup::ffmpegFixup seg:", segmentNumber);
            /* Fix ffmpeg segment sequence and base time in place */
            auto fixup_ret = FFmpegFixup::ffmpegFixup((char*)segData.data(), fsize, segmentNumber, media_type.c_str(),chunks_or_frames);
            if (fixup_ret < 0) {
                return ctx->make_error("Failed to fix segment", -8);
            }
        }
        const char* response_template = R"({"http" : {"status" : %d, "headers" : {"Content-Type" : ["%s"], "Content-Length" :  ["%d"]} }})";
        auto response = string_format(response_template, 200, media_type.c_str(), fsize);
        nlohmann::json j_callback = json::parse(response);
        ctx->Callback(j_callback);
		auto ret = ctx->WriteOutput(segData, fsize);
        return ctx->make_success();
    }

};
