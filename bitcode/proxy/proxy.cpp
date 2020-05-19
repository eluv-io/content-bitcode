#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/utils.h"
#include "eluvio/cddl.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"
#include "eluvio/media.h"
#include "eluvio/base64.h"
#include "eluvio/replacement.h"

using namespace elv_context;

class proxy_engine_class{
public:
    proxy_engine_class(BitCodeCallContext* ctx, JPCParams& p) : m_ctx(ctx), m_p(p){
    }
    elv_return_type do_proxy(){
        try{
            auto qp = m_ctx->QueryParams(m_p);
            if (qp.second.IsError()){
                return m_ctx->make_error("getting Query Params", qp.second);
            }
            auto params = m_ctx->SQMDGetJSON("/request_parameters");
            if (params.second.IsError()){
                return m_ctx->make_error("unable to locate request_parameters in meta", params.second);
            }
            replacement_map missing;
            auto replace = params.first.dump();
            auto replaced_json = nlohmann::json::parse(replacer::replace_all(replace, qp.first, missing));
            if (missing.size() != 0){
                for (auto el : missing)
                    LOG_WARN(m_ctx, "replacement not used", "key", el.first, "val", el.second);
            }
            auto retval = m_ctx->ProxyHttp(replaced_json);
            if (retval.second.IsError()){
                return m_ctx->make_error("ProxyHttp failure", retval.second);
            }
            retval.first["body"] = base64::decode(retval.first["body"].get<std::string>());
            std::stringstream  oss;
            oss    << retval.first.dump();
            std::vector<unsigned char> json_bytes;
            copy(istream_iterator<unsigned char>(oss), istream_iterator<unsigned char>(), back_inserter(json_bytes));
            auto p = m_ctx->Callback(200, "application/json", json_bytes.size());
            if (p.second.IsError()){
                return m_ctx->make_error("callback failure", p.second);
            }
            p = m_ctx->WriteOutput(json_bytes);
            if (p.second.IsError()){
                return m_ctx->make_error("writeoutput failure", p.second);
            }

           return m_ctx->make_success();
        }
        catch (json::exception& e){
            return m_ctx->make_error("do_proxy excpetion occured", E(e.what()));
        }
        catch (std::exception &e) {
            return m_ctx->make_error("do_proxy std excpetion occured", e.what());
        }
        catch(...){
            return m_ctx->make_error("do_proxy ... excpetion occured", E("elipsis catch handler make_entrants"));
        }
    }
protected:
    BitCodeCallContext* m_ctx;
    JPCParams& m_p;
};


elv_return_type content(BitCodeCallContext* ctx, JPCParams& p){
    auto path = ctx->HttpParam(p, "path");
    if (path.second.IsError()){
        return ctx->make_error("getting path from JSON", path.second);
    }

    char* request = (char*)(path.first.c_str());

    proxy_engine_class pe(ctx,p);

    if (strcmp(request, "/proxy") == 0)  // really need to return error if not matching any
      return pe.do_proxy();
    else{
        const char* msg = "unknown  service requested must be /challenge";
        return ctx->make_error(msg, E(msg).Kind(E::Invalid));
    }
}

int cddl_num_mandatories = 4;
char *cddl = (char*)"{"
    "\"title\" : bytes,"
    "\"description\" : text,"
    "\"video\" : eluv.video,"
    "\"image\" : eluv.img"
    "}";

/*
 * Validate content components.
 *
 * Returns:
 *  -1 in case of unexpected failure
 *   0 if valid
 *  >0 the number of validation problems (i.e. components missing or wrong)
 */
elv_return_type validate(BitCodeCallContext* ctx, JPCParams& p){
    int found = cddl_parse_and_check(ctx, cddl);

    char valid_pct[4];
    sprintf(valid_pct, "%d", (uint16_t)(found * 100)/cddl_num_mandatories);

    std::vector<uint8_t> vec(valid_pct, valid_pct + 4);
    auto res = ctx->WriteOutput(vec);
    if (res.second.IsError()){
        return ctx->make_error("write output failed", res.second);
    }
    return ctx->make_success();
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(validate)
END_MODULE_MAP()