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
template <class TDasher=Dasher>
class DashSegmentGenerator{
private:
    TDasher& p;

public:
    DashSegmentGenerator(TDasher& p) : p(p) {}
    elv_return_type dashSegment(BitCodeCallContext* ctx){
        using defer = shared_ptr<void>;
        CommandlineGenerator<true> clg(ctx, p.p);
        std::vector<std::string> v2 = clg.Flatten();

        int elementCount2 = v2.size();
        char** ppv2 = (char**)malloc(elementCount2*sizeof(char*)); //enough to hold entire vector of strings

        for (int i2=0;i2<elementCount2;i2++){
            ppv2[i2] = (char*)malloc((v2[i2].length()+1)*sizeof(char));
            strcpy(ppv2[i2], v2[i2].c_str());
        }
        std::string strPL = p.p.partListToJSONString();
        LOG_INFO(ctx, "dashSegment", "PartList",strPL);

        nlohmann::json j;
        auto inputStream = ctx->NewStream();
        auto outputStream = ctx->NewStream();
        defer _(nullptr, [ctx, inputStream,outputStream](...){
            ctx->CloseStream(inputStream);
            ctx->CloseStream(outputStream);
        });
        j["input_count"] = std::to_string(p.p.part_list.size());
        for (int iPart=0;iPart<p.p.part_list.size();iPart++){
            auto curInput = string_format("input_hash_%d", iPart) ;
            j[curInput] = p.p.part_list[iPart].phash;

        }
        j["output_init_segment"] = p.p.seg_num == "init" ? inputStream : "";
        j["output_media_segment"] = p.p.seg_num != "init" ? inputStream : "";
        j["output_manifest"] = "";
        j["output_stream_id"] = outputStream;
        j["is_video"] = (clg._params.avtype == 'v')  ? "true" : "false";

        std::string inputs = j.dump();
        auto ffmpeg_ret = ctx->FFMPEGRunEx(ppv2, elementCount2, inputs);

        if (ffmpeg_ret.second.IsError()){
            return ctx->make_error("FFPEGRun Failed", ffmpeg_ret.second);
        }
        std::string media_type = (clg._params.avtype == 'v') ? "video/mp4" : "audio/mp4";
        int chunks_or_frames = (clg._params.avtype == 'v') ? p.get_video_frames() : p.get_audio_chunks();

        for (int i=0;i<elementCount2;i++){
            free(ppv2[i]);
        }
        free(ppv2);

        int segmentNumber = 0;
        if (p.p.seg_num != "init"){
            segmentNumber = atoi(p.p.seg_num.c_str());
        }
        const int allocSize = 2000000;
        std::vector<unsigned char> curPiece(allocSize);
        std::vector<unsigned char> segData;
        int totalSize=0;
        int fsize=0;
        do{

            auto read_ret = ctx->ReadStream(outputStream, curPiece);
            if (read_ret.second.IsError()){
                return read_ret;
            }
            fsize = read_ret.first["read"];
            if (fsize < allocSize && fsize != -1)
                curPiece.resize(fsize);
            segData.insert(std::end(segData), std::begin(curPiece), std::end(curPiece));
            totalSize += fsize;
        }while(fsize == allocSize);
        if (segmentNumber != 0){
            LOG_INFO(ctx, "Calling FFmpegFixup", "seg", segmentNumber);
            /* Fix ffmpeg segment sequence and base time in place */
            auto fixup_ret = FFmpegFixup::ffmpegFixup(ctx, (char*)segData.data(), fsize, segmentNumber, media_type.c_str(),chunks_or_frames);
            if (fixup_ret < 0) {
                const char* msg = "Failed to fix segment";
                return ctx->make_error(msg, E(msg).Kind(E::Other));
            }
        }
        ctx->Callback(200, media_type.c_str(), totalSize);
        auto ret = ctx->WriteOutput(segData, totalSize);
        if (ret.second.IsError()){
           return ctx->make_error("dashsegment write output",ret.second);
        }
        return ctx->make_success(ret.first);
    }

};
