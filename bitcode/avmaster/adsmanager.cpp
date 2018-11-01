/*
 * Ads manager
 *
 * Specialized content library containing sponsor assets and metadata and logic for
 * matching them to viewer's content.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <set>
#include <nlohmann/json.hpp>
#include "eluvio/argutils.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"

using namespace elv_context;
using nlohmann::json;


/*
 * find_ads()
 *
 * Find the best ad matching a given set of tags.
 *
 * qlibid - stringized libid
 * qhash  - stringized meta hash
 * tags   - vector of strings with tags of interest
 */
std::string
find_ads(BitCodeCallContext* ctx, char* qlibid, char* qhash, std::set<std::string>& tags) {

    auto kvPkg = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"assets"));

    std::string kvPkgStr = kvPkg.get();
    auto jsonPkg = json::parse(kvPkg.get());
    std::map<string, float> matches;
    std::map<string, float>::iterator itProbs;
    float max = 0.0;
    std::string maxTag;
    std::string maxId;

    std::cout << "ADSMG assets " << jsonPkg << std::endl;

    for (auto& tag : tags) {
        // iterate the array
        for (json::iterator it = jsonPkg.begin(); it != jsonPkg.end(); ++it) {
            auto data = (*it)[tag];
            if (data != NULL){
                if (data > max){
                    maxId = it.key();
                    max = data;
                    maxTag = tag;
                }
            }
        }
    }

    std::string ad_id = maxId;
    std::string ad_contract_address = "";
    auto caddr = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"eluv.contract_address"));
    if (caddr.get() != NULL) {
        auto json_caddr = json::parse(caddr.get());
        std::cout << "ADSMG caddr " << caddr.get() << std::endl;
        ad_contract_address = json_caddr.get<std::string>();
    } else {
        std::cout << "ADSMG contract address not found" << std::endl;
    }

    /* Example response format
    {
      "asset": { "qlibid":"ilib6cKYsATH3itrt1ujaKZEQo",
                 "id" : "iq__9BoYqRzKaju1trti3HTAsYKc",  # Can be id or hash (**TODO CONVENTION**)
                 "contract_address" : "0xCCCC",          # To optimize lookup
      },
      "amount":"1.75",   # LIMITATION - returned as string due to exec engine call spec limitation
     "tag":"beer”
    }
    */
    json res_asset;
    res_asset["qlibid"] = qlibid;
    res_asset["id"] = ad_id;
    res_asset["contract_address"] = ad_contract_address;

    json res;
    res["asset"] = res_asset;
    res["tag"] = maxTag;
    res["amount"] = max;

    std::string res_str = res.dump();

    printf("ADSMG find_ads %s\n", res_str.c_str()); fflush(stdout);

    return res_str;
}

/*
 * Convert 'timecode' to milliseconds
 * Requires full format: HH:MM:SS.mmm
 * TODO support short timecode for example SS.mmm
 *
 * Return -1 on parsing error.
 */
int timecode_to_millisec(const char* timecode){
    int hour=0;
    int minutes=0;
    int sec=0;
    int milli=0;

    if (timecode == 0){
        return 0;
    }
    int rc = sscanf(timecode, "%d:%d:%d.%d", &hour, &minutes, &sec, &milli);
    if (rc < 4) {
        return -1;
    }
    return (hour*3600 + minutes*60 + sec) * 1000 + milli;
}

/*
 * Internal function - retrieve video tags from a specified content object.
 *
 * Example video tags format:
 *   "video_tags":[
 *     {"time_in" : "00:00:00.000", "time_out" : "00:01:00.000", "tags" : [
 *       {"tag" : "aaa", "score" : 0.10 }, { "tag" : "bbb", "score" : 0.20 }
 *       ]
 *     },
 *     {"time_in" : "00:01:00.000", "time_out" : "00:02:00.000", "tags" : [
 *       {"tag" : "aaa", "score" : 0.90 }
 *       ]
 *     }
 *   ]
 *
 */
std::set<std::string>
video_tags(BitCodeCallContext* ctx, char *qlibid, char *qhash, char *timecode){
    std::set<std::string> s;
    auto tagsjs = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"video_tags"));
    if (tagsjs.get() == 0 || strcmp(tagsjs.get(),"") == 0){
        printf("fabric metadata has no element video_tags or its empty\n");
        return s; //. empty
    }
    auto json_tags = json::parse(tagsjs.get());

    printf("ADSMG video_tags %s\n", tagsjs.get()); fflush(stdout);

    int time_converted = timecode_to_millisec(timecode);

    for (auto& el : json_tags){
        int in, out;
        in = timecode_to_millisec(el["time_in"].get<std::string>().c_str());
        out = timecode_to_millisec(el["time_out"].get<std::string>().c_str());
        if (timecode == NULL ||
            (in >= 0 && out >= 0 && time_converted >= 0 && in <= time_converted && out >= time_converted)) {
            for (auto& tag : el["tags"]){
                s.insert(tag["tag"].get<std::string>());
            }
        }
    }


    return s;
}

/*
 * Callable by API '/call/ads'
 *
 * /call/ads?qlibid="xxx"?id="id_or_hash"&time=00:00:01.000
 *
 * Aguments:
 * - qlibid
 * - qhash
 * - viewer content qlibid
 * - viewer content content id
 * - viewer timecode (optional)
 *
 * Returns the required 'output spec' for the '/call' API
 * - content type 'application/json'
 * - list of keys and values to be represented as JSON "key":"value" in the return body
 */
std::pair<nlohmann::json, int> ads(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }

    auto it_libid = params._map.find("qlibid");
    if (it_libid == params._map.end()){
        printf("libid not provided\n");
        return ctx->make_error("libid not provided", -1);
    }

    auto it_qhash = params._map.find("qwtoken");
    if (it_qhash == params._map.end()){
        printf("qhash not provided\n");
        return ctx->make_error("qhash not provided", -2);
    }

    auto it_viewer_libid = params._map.find("viewer_qlib_id");
    if (it_libid == params._map.end()){
        printf("viewer libid not provided\n");
        return ctx->make_error("viewer libid not provided", -3);
    }

    auto it_viewer_id = params._map.find("viewer_id");
    if (it_viewer_id == params._map.end()){
        printf("viewerid not provided\n");
        return ctx->make_error("viewerid not provided", -4);
    }
    auto it_viewer_timecode = params._map.find("viewer_timecode");
    if (it_viewer_timecode == params._map.end()){
        printf("viewer timecode not provided\n");
        return ctx->make_error("viewer timecode not provided", -5);
    }

    char *qlibid = (char*)it_libid->second.c_str();
    char *qhash = (char*)it_qhash->second.c_str();
    printf("ADSMG lib=%s qhash=%s\n", qlibid, qhash);fflush(stdout);

    char *viewer_qlibid = (char*)it_viewer_libid->second.c_str();
    char *viewer_id = (char*)it_viewer_id->second.c_str();
    char *viewer_timecode = (char*)it_viewer_timecode->second.c_str();

    printf("ADSMG viwer qlibid=%s id=%s\n", viewer_qlibid, viewer_id);

    std::set<std::string> tags = video_tags(ctx, viewer_qlibid, viewer_id, viewer_timecode);

    std::string res_str = find_ads(ctx, qlibid, qhash, tags);

    char *headers = (char *)"application/json";

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    j["body"] = res_str;
    return std::make_pair(j,0);
}

/*
 * Callable by API '/call/register_asset'
 *
 * /call/register?id="id_or_hash"&time=00:00:01.000
 *
 * Aguments:
 * - qlibid
 * - qhash
 * - asset content qlibid
 * - asset content content id
 *
 * Returns the required 'output spec' for the '/call' API
 * - content type 'application/json'
 * - empty response
 */
std::pair<nlohmann::json, int> register_asset(BitCodeCallContext* ctx, JPCParams& p){
    HttpParams params;
    auto p_res = params.Init(p);
    if (p_res.second != 0){
        return ctx->make_error(p_res.first, p_res.second);
    }
    auto it_libid = params._map.find("qlibid");
    if (it_libid == params._map.end()){
        printf("libid not provided\n");
        return ctx->make_error("libid not provided", -1);
    }

    auto it_qhash = params._map.find("qwtoken");
    if (it_qhash == params._map.end()){
        printf("qhash not provided\n");
        return ctx->make_error("qhash not provided", -2);
    }

    char *qlibid = (char*)it_libid->second.c_str();
    char *qhash = (char*)it_qhash->second.c_str();
    printf("ADSMG lib=%s qhash=%s\n", qlibid, qhash);fflush(stdout);


    auto assetsjs = CHAR_BASED_AUTO_RELEASE(ctx->SQMDGetJSON((char*)"assets"));

    printf("ADSMG assets %s\n", assetsjs.get()); fflush(stdout);

    auto it_asset_qlibid = params._map.find("asset_qlib_id");
    if (it_asset_qlibid == params._map.end()){
        printf("asset libid not provided\n");
        return ctx->make_error("asset libid not provided", -3);
    }
    auto it_asset_id = params._map.find("asset_id");
    if (it_asset_id == params._map.end()){
        printf("asset id not provided\n");
        return ctx->make_error("assetid not provided", -4);
    }

    char *asset_qlibid = (char*)it_asset_qlibid->second.c_str();
    char *asset_id = (char*)it_asset_id->second.c_str();;


    printf("ADSMG asset qlibid=%s id=%s\n", asset_qlibid, asset_id);

    // TODO - add asset to 'asset' metadata list

    char *headers = (char *)"application/json";

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    return std::make_pair(j,0);
}

std::pair<nlohmann::json,int> content(BitCodeCallContext* ctx, JPCParams&)
{
    char *headers = (char *)"application/json";

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    return std::make_pair(j,0);
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ads)
    MODULE_MAP_ENTRY(register_asset)
END_MODULE_MAP()

