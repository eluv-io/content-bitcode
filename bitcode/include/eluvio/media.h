#pragma once
#include "eluvio/bitcode_context.h"
#include "eluvio/el_constants.h"

using namespace elv_context;

namespace elv_media_fns{
elv_return_type make_image(BitCodeCallContext* ctx){
    char *headers = (char *)"image/png";

    LOG_DEBUG(ctx, "make_image thumbnail from image");
    auto phash = ctx->SQMDGetString((char*)eluvio_image_key);
    if (phash == "") {
        const char* msg = "make_image";
        return ctx->make_error(msg, E(msg, "image", eluvio_image_key));
    }
    LOG_INFO(ctx, "make_image thumbnail",  "qphash", phash.c_str());

    /* Read the part in memory */
    uint32_t psz = 0;
    auto body = ctx->QReadPart(phash.c_str(), 0, -1, &psz);
    if (body->size() == 0) {
        const char* msg = "Failed to read resource part";
        LOG_ERROR(ctx, msg, "HASH", phash.c_str());
        return ctx->make_error(msg, E(msg, "HASH", phash.c_str()));
    }
    LOG_INFO(ctx, "make_image",  "thumbnail_part_size", (int)psz);

    ctx->Callback(200, headers, psz);
    auto ret = ctx->WriteOutput(*body);

    return ctx->make_success();
}

elv_return_type load_all_data_from_stream(BitCodeCallContext* ctx, const char* stream_name, std::vector<uint8_t>& total){
    const int allocSize = 1000000;
    std::vector<unsigned char> segData(allocSize);
    int fsize=0;
    do{
        auto read_ret = ctx->ReadStream(stream_name, segData);
        if (read_ret.second.IsError()){
           const char* msg = "Failed to Load data from stream";
           return ctx->make_error(msg,E(msg, "stream_name", stream_name));
        }
        fsize = read_ret.first["read"];
        if (fsize < allocSize && fsize != -1)
            segData.resize(fsize);
        total.insert(std::end(total), std::begin(segData), std::end(segData));
    }while(fsize == allocSize);
    return ctx->make_success();
}

// make_sum creates a checksum (currently MD5 or SHA256) of either the given
// content part or bundle file path
//
// - method:    checksum method (MD5 or SHA256)
// - qihot:     content ID, hash or token
// - qphash:    content part hash
// - file_path: bundle file path
// - qlibid:    optional content library ID if content is in a different content
//              library than the one linked to the call context
// **NOTE qphash and file_path are mutually exclusive. combining is an error

elv_return_type make_sum(BitCodeCallContext* ctx, JPCParams& p){
    auto params = ctx->QueryParams(p);
    if (params.second.IsError()){
        return ctx->make_error("make_sum failed to parse query params", params.second);
    }
    auto paramMap = params.first;
    auto m = paramMap.find("method");
    auto method = (m == paramMap.end()) ? "MD5" : m->second;
    auto qihot = paramMap.find("qihot");
    if (qihot == paramMap.end()){
        const char* msg = "qihot not specified";
        return ctx->make_error(msg,E(msg));
    }
    std::string qlibid = "";
    auto lib_id = paramMap.find("qlibid");
    if (lib_id != paramMap.end()){
        qlibid = lib_id->second;
    }
    auto qphash = paramMap.find("qphash");
    if (qphash == paramMap.end()){
        auto file_path = paramMap.find("file_path");
        if (file_path == paramMap.end()){
            const char* msg = "no file_path or qphash specified";
            return ctx->make_error(msg,E(msg));
        }else{
            return ctx->CheckSumFile(method, file_path->second, qihot->second, qlibid);
        }
    }
    return ctx->CheckSumPart(method, qphash->second, qihot->second, qlibid);
}

};

