#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <limits.h>
#include <unistd.h>
#include <algorithm>
#include "eluvio/argutils.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/utils.h"

using namespace elv_context;

std::pair<nlohmann::json,int>
hello(BitCodeCallContext* ctx, JPCParams& params)
{
    nlohmann::json j;
    j["headers"] = "application/json";
    j["sum"] = 8;
    return std::make_pair(j,0);
}

std::pair<nlohmann::json,int>
logTest(BitCodeCallContext* ctx, JPCParams& params)
{
    LOG_DEBUG(ctx, "log debug", "string-field", "a string!", "int-field", 12);
    LOG_INFO(ctx, "log info", "string-field", "a string!", "int-field", 12);
    LOG_WARN(ctx, "log warn - no args");
    LOG_ERROR(ctx, "log error", "string-field", "a string!", "int-field", 12);

    return std::make_pair(nullptr,0);
}

std::pair<nlohmann::json,int>
taggit(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
         return ctx->make_error(p_res.first, p_res.second);
    }
    char* inputs[] = {
        (char*)"run",
        (char*)"--rm",
        (char*)"--mount",
        (char*)"src=%s,target=/data,type=bind",
        (char*)"video_tagging:latest",
        (char*)"MISSED"
    };
    char CWD[1024];

    if (getcwd(CWD, sizeof(CWD)) == NULL){
        printf("unable to get full path info for temp");
    }
    std::string dir = CWD;
    dir += "/testdata/";
    char src_directory[1024];
    sprintf(src_directory, inputs[3], dir.c_str());
    inputs[3] = src_directory;
    inputs[5] = (char*) "StarWars_EN_video_master_tiny_20sec.mp4";

    auto taggerRes = ctx->TaggerRun(inputs, sizeof(inputs)/sizeof(char*));
    nlohmann::json j;
    j["headers"] = "application/json";
    j["result"] = taggerRes;
    return std::make_pair(j,0);
}

template<typename T>
bool AreVectorsSame(const std::vector<T>& i_Vec1,const std::vector<T>& i_Vec2)
{
if(i_Vec1.size()!=i_Vec2.size())
return false;

for(size_t i=0;i<i_Vec1.size();i++)
if(i_Vec1[i]!=i_Vec2[i])
return false;

return true;
}

std::pair<nlohmann::json,int>
part_to_file(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
         return ctx->make_error(p_res.first, p_res.second);
    }
    std::cout << "Hello World\n";
    auto f = ctx->NewFileStream();

    std::cout << f["stream_id"] << std::endl;
    for (auto& el : params._map)
    std::cout << el.first <<  " second=" << el.second << std::endl;
    ctx->WritePartToStream(f["stream_id"], params._map["qihot"], params._map["qpinfo"]);
    std::cout << "RETURNED FROM WRITE\n";
    ctx->CloseStream(f["stream_id"]);
    // auto f = ctx->NewFileStream();

    // std::cout << "s=" << s << "\nf=" << f << std::endl;
    // std::vector<uint8_t> vec = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18};
    // ctx->WriteStream(f["stream_id"], vec);
    // ctx->WriteStream(s["stream_id"],vec);
    // printf("After close\n");
    // ctx->CloseStream(f["stream_id"]);


    // std::vector<uint8_t> vec_ret(vec.size());
    // ctx->ReadStream(s["stream_id"],vec_ret);
    // ctx->CloseStream(s["stream_id"]);

    nlohmann::json j;
    j["headers"] = "application/json";
    j["result"] = 0;
    return std::make_pair(j,0);
}


std::pair<nlohmann::json,int>
file_test(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
         LOG_WARN(ctx, "invalid HttpParams", "error", p_res.first)
         return ctx->make_error(p_res.first, p_res.second);
    }
    auto s = ctx->NewStream();
    auto f = ctx->NewFileStream();

    std::cout << "s=" << s << "\nf=" << f << std::endl;
    std::vector<uint8_t> vec = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18};
    ctx->WriteStream(f["stream_id"], vec);
    ctx->WriteStream(s["stream_id"],vec);
    printf("After close\n");
    ctx->CloseStream(f["stream_id"]);


    std::vector<uint8_t> vec_ret(vec.size());
    ctx->ReadStream(s["stream_id"],vec_ret);
    ctx->CloseStream(s["stream_id"]);

    nlohmann::json j;
    j["headers"] = "application/json";
    j["result"] = AreVectorsSame(vec,vec_ret) ? 0 : 1;
    return std::make_pair(j,0);
}

bool compareFiles(const std::string& p1, const std::string& p2) {
  std::ifstream f1(p1, std::ifstream::binary|std::ifstream::ate);
  std::ifstream f2(p2, std::ifstream::binary|std::ifstream::ate);

  if (f1.fail() || f2.fail()) {
    return false; //file problem
  }

  if (f1.tellg() != f2.tellg()) {
    return false; //size mismatch
  }

  //seek back to beginning and use std::equal to compare contents
  f1.seekg(0, std::ifstream::beg);
  f2.seekg(0, std::ifstream::beg);
  return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                    std::istreambuf_iterator<char>(),
                    std::istreambuf_iterator<char>(f2.rdbuf()));
}

std::pair<nlohmann::json,int> dash_test(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    const char* inputArray[] = { "-hide_banner", "-nostats", "-y" , "-ss","0.00", "-to","6.06", "-i", "INPUT_FILE", "-map",  "0:v",
                                 "-vcodec", "libx264", "-b:v", "1000", "-x264opts", "keyint=144:min-keyint=144:scenecut=-1",
                                 "-f", "dash", "-min_seg_duration", "6006.0", "-use_template", "0", "-use_timeline", "0",
                                 "-remove_at_exit", "0", "-init_seg_name", "dummy", "-media_seg_name", "OUTPUT-FILE", "testdata/dummy-v"};

    int array_count = sizeof(inputArray)/sizeof(const char*);

    if (params._map.find("input_file") == params._map.end() ||
        params._map.find("reference_file") == params._map.end() ||
        params._map.find("output_file") == params._map.end()){
            return ctx->make_error("missing parameter to test", -11);
    }
    auto output_filename = params._map["output_file"];
    auto input_filename = params._map["input_file"];
    auto reference_filename = params._map["reference_file"];

    inputArray[array_count-2] = output_filename.c_str();
    inputArray[8] = input_filename.c_str();

    auto ffmpegRet = ctx->FFMPEGRun((char**)inputArray, array_count);

     if (ffmpegRet != 0){
         // ERROR
        return ctx->make_error("FFMpeg failed to run", -2);
     }
     nlohmann::json j;
     std::string compare_file = "./testdata/";
     compare_file += output_filename;
     if (compareFiles(compare_file, reference_filename))
        j["result"] = 0;
    else
        j["result"] = 99;

    j["headers"] = "application/json";
    return std::make_pair(j,0);

}

std::pair<nlohmann::json,int>
transcode(BitCodeCallContext* ctx, JPCParams& p)
{
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }

    char* inputsSegment[] = {
        (char*)"-hide_banner",
        (char*)"-nostats",
        (char*)"-y",
        (char*)"-loglevel",
        (char*)"debug",
        (char*)"-i",
        (char*)"MISSED",
        (char*)"-ss",
        (char*)"MISSED",
        (char*)"-to",
        (char*)"MISSED",
        (char*)"-c:v",
        (char*)"copy",
        (char*)"-c:a",
        (char*)"copy",
        (char*)"MISSED"};

    char* inputsFullVideo[] = {
        (char*)"-hide_banner",
        (char*)"-nostats",
        (char*)"-y",
        (char*)"-loglevel",
        (char*)"debug",
        (char*)"-i",
        (char*)"MISSED",
        (char*)"-ss",
        (char*)"MISSED",
        (char*)"-c:v",
        (char*)"copy",
        (char*)"-c:a",
        (char*)"copy",
        (char*)"MISSED"};

    char** inputs;
    int cEls;

    if (params._map.find("input_file") == params._map.end() ||
        params._map.find("start_time") == params._map.end() ||
        params._map.find("reference_file") == params._map.end() ||
        params._map.find("output_file") == params._map.end()){
            return ctx->make_error("missing parameter to test", -11);
    }

    std::string end_time;

    if(params._map.find("end_time") == params._map.end()){
        inputs = inputsFullVideo;
        cEls = sizeof(inputsFullVideo)/sizeof(const char*);
        end_time = "";
    }else{
        inputs = inputsSegment;
        cEls = sizeof(inputsSegment)/sizeof(const char*);
        end_time = params._map["end_time"];
    }

    std::string input_filename = params._map["input_file"];
    std::string output_filename = params._map["output_file"];
    std::string start_time = params._map["start_time"];
    std::string reference_file = params._map["reference_file"];


    if (end_time == ""){
        inputs[6] = (char*)input_filename.c_str();
        inputs[8] = (char*)start_time.c_str();
        inputs[13] = (char*)output_filename.c_str();

    }else{
        inputs[6] = (char*)input_filename.c_str();
        inputs[8] = (char*)start_time.c_str();
        inputs[10] = (char*)end_time.c_str();
        inputs[15] = (char*)output_filename.c_str();
    }

    // char inputArray[65536];
    // int arrayLen = MemoryUtils::packStringArray(inputArray, inputs, cEls);

     auto ffmpegRet = ctx->FFMPEGRun(inputs, cEls);

     if (ffmpegRet != 0){
         // ERROR
        return ctx->make_error("FFMpeg failed to run", -2);
     }

     nlohmann::json j;

     if (compareFiles(output_filename, reference_file))
        j["result"] = 0;
    else
        j["result"] = 99;

    j["headers"] = "application/json";
//    j["sum"] = 8;
    return std::make_pair(j,0);
}

std::pair<nlohmann::json,int>
httpEcho(BitCodeCallContext* ctx, JPCParams& p)
{
    nlohmann::json j = {
        {"http", {
            {"status", 200},
            {"headers", {
                {"X-ELUVIO-EXTRA-RESPONSE", {"EXTRA-HEADER-VALUE"}}
            }}
        }}
    };

    for (auto& el : p["http"]["headers"].items()) {
        std::string prefix = "X-Eluvio-";
        if(el.key().substr(0, prefix.size()) == prefix) {
            j["http"]["headers"][el.key()] = el.value();
        }
    }

    // add content length and byte-range info (if any) to callback args for
    // byte-range handling, which will:
    // - calculate the effective offset and length based on range header and
    //   content length
    // - set the correct Content-Range header and status (e.g. 206 Partial Content)
    // - return adapted byte range - see below
    j["http"]["content_length"] = p["http"]["content_length"];
    j["http"]["byte_range"] = p["http"]["byte_range"];

    auto cbRes = ctx->Callback(j);
    if (cbRes.second != 0){
        return ctx->make_error(cbRes.first, cbRes.second);
    }

    long off = 0;
    long len = LONG_MAX;

    auto range = cbRes.first["http"]["adapted_byte_range"];
    if (range != nullptr) {
        // a byte range header was specified in the request, and the returned
        // range provides the adapted offset and length
        LOG_INFO(ctx, "byte range specified", "byte_range", range)
        if (range["invalid"]) {
            // the range was invalid, and error response was already sent
            return ctx->make_error("invalid byte range", cbRes.second);
        }
        off = range["off"];
        len = range["len"];
    }

    std::vector<unsigned char> buf(1024);
    int read = 0;
    do {
        auto res = ctx->ReadInput(buf);
        if (res.second != 0){
            return ctx->make_error("failed to read from input", -99);
        }
        read = (int) res.first["read"];

        if (read > 0) {
            std::pair<nlohmann::json, int> w;
            if (off > 0) {
                // skip 'off' bytes
                if (off >= read) {
                    off -= read;
                    continue;
                }
                auto it = buf.begin();
                it += off;
                std::vector<unsigned char> sbuf(it, buf.end());
                read -= off;
                off = 0;
                w = ctx->WriteOutput(sbuf, std::min(len, (long)read));
            } else {
                // regular write
                w = ctx->WriteOutput(buf, std::min(len, (long)read));
            }
            len -= read;
        }
    } while(read >= 0 && len > 0);

    // ctx->CloseStream(ctx->Input());
    // ctx->CloseStream(ctx->Output());

    nlohmann::json res;
    return std::make_pair(res,0);
}

std::pair<nlohmann::json,int>
callGo(BitCodeCallContext* ctx, JPCParams& p)
{
    auto method = p["function"].get<std::string>();
    auto module = p["module"].get<std::string>();

    return ctx->Call(method, p["args"], module);
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(hello)
    MODULE_MAP_ENTRY(httpEcho)
    MODULE_MAP_ENTRY(transcode)
    MODULE_MAP_ENTRY(taggit)
    MODULE_MAP_ENTRY(dash_test)
    MODULE_MAP_ENTRY(file_test)
    MODULE_MAP_ENTRY(part_to_file)
    MODULE_MAP_ENTRY(callGo)
    MODULE_MAP_ENTRY(logTest)
END_MODULE_MAP()
