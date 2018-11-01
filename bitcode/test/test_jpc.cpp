#include <string>
#include <vector>
#include "eluvio/argutils.h"
#include <nlohmann/json.hpp>


extern "C" int _JPC(int cb, char* out, char* in){
	ArgumentBuffer args(in);
    auto j = nlohmann::json::parse(args[0]);

    memset(out, 0, cb);
    nlohmann::json j_response;
    nlohmann::json j_result;
    j_result["sum"] = 8;
    j_response["id"] = j["id"];
    j_response["jpc"] = j["1.0"];
    j_response["result"] = j_result;


    std::vector<std::string> vec;
    vec.push_back(j_response.dump());
    ArgumentBuffer::argv2buf(vec, out, cb);
    return 0;
}

