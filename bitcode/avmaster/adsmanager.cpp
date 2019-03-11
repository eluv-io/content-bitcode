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
#include <sstream>
#include <iostream>

#include "nlohmann/json.hpp"
#include "eluvio/argutils.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"

using namespace elv_context;
using nlohmann::json;
typedef std::map<std::string, std::string>  AdsMap;

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
find_ads(BitCodeCallContext* ctx, std::set<std::string>& tags) {

    auto assets = ctx->SQMDGetJSON("assets");
    if (assets.second.IsError()){
        LOG_ERROR(ctx, "getting assets", "inner_error", assets.second.getJSON());
        return "";
    }

    auto& jsonPkg = assets.first;
    std::map<string, float> matches;
    std::map<string, float>::iterator itProbs;
    float max = 0.0;
    std::string maxTag;
    std::string maxId;

    LOG_DEBUG(ctx, "AdsManager:", "assets", jsonPkg.dump());

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
    auto caddr = ctx->SQMDGetJSON("eluv.contract_address");
    if (!caddr.second.IsError()) {
        auto& json_caddr = caddr.first;
        LOG_DEBUG(ctx, "AdsManager:", "caddr",(const char*)json_caddr.dump().c_str());
        ad_contract_address = json_caddr.get<std::string>();
    } else {
        LOG_DEBUG(ctx, "AdsManager:", "contract address not found");
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
    res_asset["qlibid"] = ctx->LibId();
    res_asset["id"] = ad_id;
    res_asset["contract_address"] = ad_contract_address;

    json res;
    res["asset"] = res_asset;
    res["tag"] = maxTag;
    res["amount"] = max;

    std::string res_str = res.dump();

    LOG_DEBUG(ctx, "AdsManager:", "JSON", res_str.c_str());;

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
std::pair<std::map<std::string, double>, E>
commonTags(BitCodeCallContext* ctx, nlohmann::json& json_tags, std::string&  timein, std::string&  timeout){
    LOG_DEBUG(ctx, "commonTags:", "video_tags", json_tags.dump());
    std::map<std::string, double> s;
    int timein_converted = timecode_to_millisec(timein.c_str());
    int timeout_converted = timecode_to_millisec(timeout.c_str());

    for (auto& el : json_tags){
        int in, out;
        in = timecode_to_millisec(el["time_in"].get<std::string>().c_str());
        out = timecode_to_millisec(el["time_out"].get<std::string>().c_str());
        if (in >= timein_converted && out <= timeout_converted) {
            for (auto& tag : el["tags"]){
                if (tag["score"].type() != nlohmann::detail::value_t::number_float){
                    return std::make_pair(s, E("common tags parsing", "tag", tag.dump()));
                }
                s[tag["tag"].get<std::string>()] = tag["score"].get<double>();
            }
        }
    }
    return std::make_pair(s, E(false));
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
std::pair<std::map<std::string, double>, E>
video_tags(BitCodeCallContext* ctx, std::string& qlibid, std::string&  qhash, std::string&  timein, std::string&  timeout){
    LOG_DEBUG(ctx, "video_tags");
    std::map<std::string, double> s;
    auto tagsjs = ctx->SQMDGetJSON(qlibid.c_str(), qhash.c_str(), "video_tags");
    if (tagsjs.second.IsError()){
        LOG_WARN(ctx, "fabric metadata has no element video_tags or its empty");
        return std::make_pair(s,E(false)); //. empty
    }
    return commonTags(ctx, tagsjs.first, timein, timeout);
}


std::pair<std::map<std::string, double>, E>
video_tags(BitCodeCallContext* ctx, std::string hash, std::string&  timein, std::string&  timeout){
    LOG_DEBUG(ctx, "video_tags");
    std::map<std::string, double> s;
    auto tagsjs = ctx->SQMDGetJSON(hash.c_str(), "video_tags");
    if (tagsjs.second.IsError()){
        LOG_WARN(ctx, "fabric metadata has no element video_tags or its empty");
        return std::make_pair(s,E(false)); //. empty
    }
    return commonTags(ctx, tagsjs.first, timein, timeout);
}

elv_return_type GetAdWeights(BitCodeCallContext* ctx, std::string& params){
    std::istringstream p(params);
    std::string sParseComma;
    nlohmann::json j;
    AdsMap mapRet;
    while (getline(p, sParseComma, ',')) {
        if (strchr(sParseComma.c_str(), ':') != 0){
            auto colonLoc = sParseComma.find(':');
            auto key = sParseComma.substr(0, colonLoc);
            auto value = sParseComma.substr(colonLoc+1, sParseComma.size() - colonLoc);
            LOG_DEBUG(ctx, "parsing tags", "key", key, "value", value);
            mapRet[key] = value;
        }else{
            mapRet[sParseComma] = "100";
        }

    }
    j["return"] = mapRet;
    return ctx->make_success(j);
}

void addToMap(std::map<std::string, double>& dst,std::map<std::string, double>& src){
    for (auto& el : src){
        if (dst.find(el.first) == dst.end()){
            src[el.first] = el.second;
        }else{ // found choose bigger
            if (dst[el.first] > src[el.first]){
                src[el.first] = el.second;
            }
        }
    }
}

bool compareMatches(std::map<std::string, string>& left, std::map<std::string, string>& right){
    try{
        auto lVal = atof(left["payout"].c_str());
        auto rVal = atof(right["payout"].c_str());
        auto lScore = atof(left["score"].c_str());
        auto rScore = atof(right["score"].c_str());
        if (lVal*lScore >= rVal*rScore)
            return true;
        return false;
    }
    catch(std::exception& e){
        if (left.find("ctx") != left.end()){
            BitCodeCallContext* ctx = (BitCodeCallContext*)atol(left["ctx"].c_str());
            LOG_ERROR(ctx, "compareMatches threw exception", "what", e.what());
        }
    }
    return false;

}

/*
 * Callable by API '/call/ads'
 *
 * /call/ads?qlibid="xxx"?id="id_or_hash"&time=00:00:01.000
 *
 * Aguments:
 * - format
 * - library
 * - content_id
 * - time_in
 * - time_out
 *  - max_ads (optional default to 1)
 *
 * Returns the required 'output spec' for the '/call' API
 * - content type 'application/json'
 * - list of keys and values to be represented as JSON "key":"value" in the return body
 */
elv_return_type ads(BitCodeCallContext* ctx, JPCParams& p){
    auto params = ctx->QueryParams(p);
    std::string time_in = "00:00:00.000";
    std::string time_out = "99:59:59.999";
    AdsMap adMap;
    std::map<std::string,double> videoTags;
    auto ad_count = 1; //default
    if (params.second.IsError()){
        return ctx->make_error("Query Parameters from JSON", params.second);
    }

    auto it_viewer_timein = params.first.find("time_in");
    if (it_viewer_timein == params.first.end()){
        LOG_DEBUG(ctx, "timein not provided");
    }else{
       time_in = it_viewer_timein->second;
    }

    auto it_viewer_timeout = params.first.find("time_out");
    if (it_viewer_timeout == params.first.end()){
        LOG_DEBUG(ctx, "timeout not provided");
    }else{
       time_out = it_viewer_timeout->second;
    }
    auto it_ad_count = params.first.find("max_ads");
    if (it_ad_count == params.first.end()){
        LOG_DEBUG(ctx, "max_ads not provided", "max_ads", ad_count);
    }else{
        ad_count = atoi(it_ad_count->second.c_str());
    }

    auto campaigns_library = ctx->SQMDGetString("campaigns_library");
    if (campaigns_library == ""){
        const char* msg = "Could not find campaigns_library";
        ctx->make_error(msg, E(msg));
    }

    auto it_tags = params.first.find("tags");
    if (it_tags == params.first.end()){
        LOG_DEBUG(ctx, "tags not provided");
    }else{
        auto ret = GetAdWeights(ctx, it_tags->second);
        if (ret.second.IsError())
            return ctx->make_error("ads", ret.second);
        adMap = ret.first["return"].get<AdsMap>();
    }


    auto it_content_hash = params.first.find("content_hash");
    if (it_content_hash != params.first.end()){
        LOG_DEBUG(ctx, "ads", "content_hash", it_content_hash->second);
        auto vtRet  = video_tags(ctx, it_content_hash->second.c_str(), time_in,time_out);
        if (vtRet.second.IsError())
            return ctx->make_error("ads failed to parse video_tags", vtRet.second);
        videoTags = vtRet.first;
    }else{
        auto it_viewer_lib = params.first.find("library");
        if (it_viewer_lib == params.first.end()){
            const char* msg="library not provided";
            return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));
        }
        auto it_viewer_content_id = params.first.find("content_id");
        if (it_viewer_content_id == params.first.end()){
            const char* msg="content_id not provided";
            return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));
        }
        auto viewer_id = it_viewer_lib->second;
        auto viewer_content_id = it_viewer_content_id->second;

        LOG_DEBUG(ctx, "AdsManager Viewer:" ,"qlibid", campaigns_library, "id", viewer_id);
        auto vtRet = video_tags(ctx, viewer_id, viewer_content_id, time_in,time_out);
        if (vtRet.second.IsError())
            return ctx->make_error("ads failed to parse video_tags", vtRet.second);
        videoTags = vtRet.first;
    }

    auto libsRet = ctx->QListContentFor(campaigns_library);

    if (libsRet.second.IsError()){
        const char* msg = "Unable to gets list of contents";
        return ctx->make_error(msg, libsRet.second);
    }
    auto contents = libsRet.first["list"]["contents"];
    std::vector<std::map<string,string>> retVal;

    for (auto &campaign : contents){
        auto cid = campaign["id"].get<std::string>();
        auto adsRet = ctx->SQMDGetJSON(campaigns_library.c_str(), cid.c_str(), "ads");
        if (adsRet.second.IsError()){
            continue;
        }
        auto &adsCur = adsRet.first;

        for (auto it  = adsCur.begin(); it != adsCur.end(); it++){
            LOG_DEBUG(ctx, "ad info", "ad", it.value());
            auto& ad = *it;
            auto adTags = ad["tags"].get<std::map<std::string, double>>();
            addToMap(adTags, videoTags);
            for (auto& el : adTags){
                if (videoTags.find(el.first) != videoTags.end() || el.first == "_other"){
                    std::map<std::string, std::string> mapEntry;
                    mapEntry["campaign_id"] = cid;
                    mapEntry["ad_library"] = ad["library"];
                    mapEntry["ad_id"] = it.key();
                    mapEntry["name"] = ad["name"];
                    mapEntry["tag"] = el.first;
                    mapEntry["payout"] = std::to_string(el.second);
                    mapEntry["score"] = std::to_string(videoTags[el.first]);
                    mapEntry["ctx"] = std::to_string((int64_t)ctx);
                    retVal.push_back(mapEntry);
                }
            }
        }
    }

    std::sort (retVal.begin(), retVal.end(), compareMatches);

    retVal.push_back(adMap);

    auto jsonRet = nlohmann::json(retVal);
    auto stringRet = jsonRet.dump();
    std::vector<unsigned char> jsonData(stringRet.c_str(), stringRet.c_str()+stringRet.size());
    ctx->Callback(200, "application/json", jsonData.size());
    auto ret = ctx->WriteOutput(jsonData);
    return ctx->make_success();
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
elv_return_type register_asset(BitCodeCallContext* ctx, JPCParams& p){
    auto params = ctx->QueryParams(p);
    if (params.second.IsError()){
        return ctx->make_error("Query Parameters from JSON", params.second);
    }
    auto it_libid = params.first.find("qlibid");
    if (it_libid == params.first.end()){
        const char* msg = "libid not provided";
        return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));
    }

    auto it_qhash = params.first.find("qwtoken");
    if (it_qhash == params.first.end()){
        const char* msg="qwtoken not provided";
        return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));
    }


    char *qlibid = (char*)it_libid->second.c_str();
    char *qhash = (char*)it_qhash->second.c_str();
    LOG_DEBUG(ctx, "Adsmanager", "qlibid", qlibid, "qhash", qhash);


    auto assetsjs = ctx->SQMDGetJSON(qlibid, qhash,"assets");

    if (assetsjs.second.IsError()){
        return ctx->make_error("getting assets", assetsjs.second);
    }

    LOG_DEBUG(ctx, "Adsmanager assets:", "JSON", assetsjs.first.dump());

    auto it_asset_qlibid = params.first.find("asset_qlib_id");
    if (it_asset_qlibid == params.first.end()){
        const char* msg = "asset libid not provided";
        return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));

    }
    auto it_asset_id = params.first.find("asset_id");
    if (it_asset_id == params.first.end()){
        const char* msg = "asset id not provided";
        return ctx->make_error(msg, E(msg).Kind(E::BadHttpParams));
    }

    char *asset_qlibid = (char*)it_asset_qlibid->second.c_str();
    char *asset_id = (char*)it_asset_id->second.c_str();


    LOG_DEBUG(ctx, "Adsmanager asset", "qlibid", asset_qlibid,  "id", asset_id);

    // TODO - add asset to 'asset' metadata list

    char *headers = (char *)"application/json";

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    j["result"] = 0;
    return ctx->make_success(j);
}

elv_return_type content(BitCodeCallContext* ctx, JPCParams&)
{
    char *headers = (char *)"application/json";

    /* Prepare output */
    nlohmann::json j;
    j["headers"] = headers;
    j["result"] = 0;
    return ctx->make_success(j);
}

BEGIN_MODULE_MAP()
    MODULE_MAP_ENTRY(content)
    MODULE_MAP_ENTRY(ads)
    MODULE_MAP_ENTRY(register_asset)
END_MODULE_MAP()

