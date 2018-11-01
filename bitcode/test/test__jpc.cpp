#include "eluvio/bitcode_context.h"
#include "eluvio/argutils.h"
#include "eluvio/cddl.h"

using namespace elv_context;

extern "C" char*    JPC(char*) { return 0;}
extern "C" char*    Write(char*, char*, char*,int) { return 0;}
extern "C" char*    Read(char*, char*, char*,int) { return 0;}

/*
clang++ -g -Wall -I content-fabric/bitcode/include -I elv-toolchain/dist/linux-glibc.2.27/include test__jpc.cpp -o test
*/

int main(int argc, char const *argv[])
{

    std::string json = R"(
    {
        "params" : {
			"http": {
				"verb": "GET",
				"path": "/asset",
				"fragment": "",
				"query": {
					"type": "video",
                    "start" : "0",
                    "end" : "50"
				},
				"headers": {}
			}
		},
        "jpc" : "1.0",
        "id" : "somefunkyid",
        "module" : "Module55",
        "method" : "test_params"
    })";
    std::vector<std::string> v;
    v.push_back(json);
    const int cb = 65536;
    char buf[cb];
    char out[cb];

    ArgumentBuffer::argv2buf(v, buf, (int)cb);
    auto res = _JPC((int)cb, out, buf);
    if (res != 0){
        std::cout << "ERROR in _JPC return" << std::endl;
    }else{
        std::cout << "SUCCESS" << std::endl;

    }
    return 0;
}


std::pair<nlohmann::json,int> test_params(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);

    std::cout << "headers=" << params._headers << std::endl;
    std::cout << "path=" << params._path << std::endl;
    std::cout << "verb=" << params._verb << std::endl;

    if (params._map.find("start") != params._map.end()){
        std::cout << "start=" << params._map["start"] << std::endl;
    }

    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }

    return ctx->make_success();
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(test_params)
END_MODULE_MAP()

