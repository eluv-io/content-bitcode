#pragma once
#include <regex>
#include "eluvio/bitcode_context.h"
#include "eluvio/el_constants.h"

using namespace elv_context;
using namespace eluvio_errors;

namespace elv_media_fns{
elv_return_type make_image(BitCodeCallContext* ctx, const char* image_key = eluvio_image_key){
    char *headers = (char *)"image/png";

    LOG_DEBUG(ctx, "make_image thumbnail from image");
    auto phash = ctx->SQMDGetString((char*)image_key);
    if (phash == "" || phash.c_str() == NULL) {
        std::string msg = string_format("make_image %s not found", image_key);
        LOG_ERROR(ctx, msg, "key", image_key);
        return ctx->make_error(msg, E(msg, ErrorKinds::NotExist));
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


elv_return_type make_string(BitCodeCallContext* ctx, const char* key_name){
    char *headers = (char *)"application/text";

    LOG_DEBUG(ctx, "make_name");
    auto phash = ctx->SQMDGetString(key_name);
    if (phash == "") {
        const char* msg = "make_name";
        return ctx->make_error(msg, E(msg, "name", key_name));
    }
    LOG_INFO(ctx, "make_name",  "qphash", phash.c_str());

    ctx->Callback(200, headers, phash.length());
    std::vector<std::uint8_t> retData(phash.c_str(), phash.c_str()+phash.length());
    auto ret = ctx->WriteOutput(retData);

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
    auto qphash = paramMap.find("qphash");
    elv_return_type ret({}, false);
    if (qphash == paramMap.end()){
        auto file_path = paramMap.find("file_path");
        if (file_path == paramMap.end()){
            const char* msg = "no file_path or qphash specified";
            return ctx->make_error(msg,E(msg));
        }else{
            ret = ctx->CheckSumFile(method, file_path->second);
        }
    }else{
        ret =  ctx->CheckSumPart(method, qphash->second);
    }
    if (ret.second.IsError()){
        return ctx->make_error("Checksum failed on go call", ret.second);
    }
    auto j_str = ret.first.dump();
    ctx->Callback(200, "application/json", j_str.length());
    std::vector<std::uint8_t> jsonData(j_str.c_str(), j_str.c_str()+j_str.length());
    auto writeRet = ctx->WriteOutput(jsonData);
    if (writeRet.second.IsError()){
        return ctx->make_error("WriteOutput failed", writeRet.second);
    }
    return ctx->make_success(ret.first);

}

// This is a helper function to convert from an ISO duration to a double
double get_duration(const std::string& input, std::string regExp = "P([[:d:]]+Y)?([[:d:]]+M)?([[:d:]]+D)?T([[:d:]]+H)?([[:d:]]+M)?([[:d:]]+S|[[:d:]]+\\.[[:d:]]+S)?")
{
    std::regex re(regExp);
    std::smatch match;
    std::regex_search(input, match, re);
    if (match.empty()) {
        std::cout << "Pattern do NOT match" << std::endl;
        return -1.0;
    }

    std::vector<double> vec = {0,0,0,0,0,0}; // years, months, days, hours, minutes, seconds

    for (size_t i = 1; i < match.size(); ++i) {

        if (match[i].matched) {
            std::string str = match[i];
            str.pop_back(); // remove last character.
            vec[i-1] = std::stod(str);
        }
    }

    double duration = 31556926   * vec[0] +  // years
                   2629743.83 * vec[1] +  // months
                   86400      * vec[2] +  // days
                   3600       * vec[3] +  // hours
                   60         * vec[4] +  // minutes
                   1          * vec[5];   // seconds

    return duration;
}

};

