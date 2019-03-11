#pragma once
#include <nlohmann/json.hpp>
#include "eluvio/el_constants.h"
#include "eluvio/bitcode_context.h"


using nlohmann::json;
using namespace elv_context;
using namespace ffmpeg_elv_constants;

struct avpart {
    elv_fp start_secs;
    elv_fp end_secs;
    std::string phash;
    std::string temp_file_name;

    void dump(BitCodeCallContext* ctx) {
        LOG_INFO(ctx, "AVPART", "start", start_secs , "end" , end_secs , " part" ,phash , " tmp", temp_file_name);
    }

    std::string toJSONString(){
        std::string part_template = R"({"start_secs":%.6f, "end_secs" : %.6f, "phash" : "%s", "temp_file_name" : "%s" })";
        return elv_context::string_format(part_template, start_secs, end_secs, phash.c_str(), temp_file_name.c_str());
    }
};

struct representation {
    std::string rep_name;
    int height;
    int width;
    int bitrate;

    void dump(BitCodeCallContext* ctx) {
        LOG_INFO(ctx, "representation", "name", rep_name, "height",height ,"widht" , width , "bitrate", bitrate);
    }
};

struct watermark_data{
    std::string text;
    std::string pos_x;
    std::string pos_y;
    std::string font_size;
    std::string font_type;
    std::string font_color;
};

typedef struct dash_params {
    char avtype; /* TODO enum */
    std::string language;
    std::string rep_name;
    std::string seg_num;
    nlohmann::json watermark;
    BitCodeCallContext* ctx;
    elv_fp seg_duration_secs;
    elv_fp decode_time_duration_secs;
    int num_frames; /* only for video */
    representation rep;
    std::vector<avpart> part_list;

    void dump() {
        LOG_INFO(ctx, "dash params", "avtype",avtype, "language",language,"rep_name",rep_name, "seg_num",seg_num,
                 "seg_duration_secs",(elv_fp)seg_duration_secs,"decode_time_duration_secs",(elv_fp)decode_time_duration_secs,
                 "num_frames", num_frames);
        rep.dump(ctx);
        for (auto& i: part_list) {
            i.dump(ctx);
        }
    }

    std::string partListToJSONString(){
        nlohmann::json j_ret = nlohmann::json::array();
        for (int i = 0; i < part_list.size(); i++){
            nlohmann::json j_obj = nlohmann::json::parse(part_list[i].toJSONString());
            j_ret += j_obj;
        }
        return j_ret.dump();
    }
}DashParams;


class Dasher {

public:
    Dasher(std::string url, std::string offering_json_str, std::string watermark_json_str) : url(url), offering_json_str(offering_json_str), watermark_json_str(watermark_json_str) {
        offering = json::parse(offering_json_str);
        watermark = json::parse(watermark_json_str);

    }
    Dasher(char type, std::string lang, std::string rep_name, std::string seg_num, std::string offering_json_str, BitCodeCallContext* ctx, std::string watermark_json_str = "{}") :
                    offering_json_str(offering_json_str), watermark_json_str(watermark_json_str) {
        p.ctx = ctx;
        _ctx = ctx;
        offering = json::parse(offering_json_str);
        watermark = json::parse(watermark_json_str);
        p.avtype = type;
        p.language = lang;
        p.rep_name = rep_name;
        p.seg_num = seg_num;
    }
    void initialize(){
        //gen_url_params();
        gen_part_list();
        gen_rep_params();
        gen_params();

        p.dump();
    }
    int get_video_frames(){
        if (offering.find("representation_info") != offering.end()){
            auto rep_info = offering["representation_info"];
            LOG_INFO(_ctx, "video_frames", "rep_info", rep_info.dump());

            if (rep_info.find("video_frames") != rep_info.end()){
                return rep_info["video_frames"];
            }else{
                LOG_ERROR(_ctx, "no video frame info found");
                return 0;
            }
        }else{
                LOG_ERROR(_ctx, "no video frame info found");
            return 0;
        }
    }
    int get_audio_chunks(){
        if (offering.find("representation_info") != offering.end()){
            auto rep_info = offering["representation_info"];
            if (rep_info.find("audio_chunks") != rep_info.end()){
                return rep_info["audio_chunks"];
            }else{
                LOG_ERROR(_ctx, "no audio chunks info found");
                return 0;
            }
        }else{
            LOG_ERROR(_ctx, "no audio chunks info found");
            return 0;
        }
    }

private:
    void gen_part_list(){
        auto start_end = segment_timeline_points();
        p.part_list = resources_for_timeline_slice(start_end.first, start_end.second);;
    }
    void gen_rep_params(){
        auto reps = offering["representation_info"];

        std::string repstr;
        switch (p.avtype) {
        case 'v':
            repstr = "video_reps";
            break;
        case 'a':
            repstr = "audio_reps";
            break;
        }
        auto type_reps = reps[repstr];
        for (auto& r: type_reps) {
            LOG_DEBUG(_ctx, "REP", "repname", p.rep_name, "r",r.dump());
            if (r["name"] == p.rep_name) {
                p.rep.rep_name = r["name"];
                if (p.avtype == 'v') {
                    p.rep.height = r["height"];
                    p.rep.width = r["width"];
                }
                p.rep.bitrate = r["bitrate"];
                break;
            }
        }
        if (p.rep.height == 0) {
            // No match
            LOG_ERROR(_ctx, "No representation match", "name", p.rep_name);
        }
    }
    void gen_params(){
        if (p.avtype == 'v')
            p.num_frames = offering["representation_info"]["video_frames"];
        p.seg_duration_secs = segment_duration();
        p.decode_time_duration_secs = decode_time_duration_secs();

        auto offer = offering["offering"];
        auto itWatermark = offer.find("watermark");
        if (itWatermark != offer.end()){
            p.watermark = offer["watermark"];
        }
        else{
            if (watermark != nullptr)
                p.watermark =  watermark;
            else
                p.watermark = json::parse("{}");
       }
    }
    void gen_url_params(){
        auto u = url;
        std::replace(u.begin(), u.end(), '-', ' ');
        std::replace(u.begin(), u.end(), '.', ' ');

        char l[128];
        char r[128];
        char s[128];
        char x[128];
        std::sscanf(u.c_str(), "%s %s %s %s", l, r, s, x);
        LOG_INFO(_ctx, "URL", "lang" , l , "rep" , r , " seg" , s , " ext" , x);

        if (!strcmp(x, "m4v"))
            p.avtype = 'v';
        else if (!strcmp(x, "m4a"))
            p.avtype = 'a';
        p.language = l;
        p.rep_name = r;
        p.seg_num = s;
    }

    elv_fp decode_time_duration_secs(){
        auto seg_seq = segment_sequence();
        elv_fp sidx_timescale = seg_seq["sidx_timescale"];
        return sidx_timescale * segment_duration();
    }
    elv_fp segment_duration(){
        std::string f;
        switch (p.avtype) {
        case 'a':
            f = "audio_seg_duration_secs";
            break;
        case 'v':
            f = "video_seg_duration_secs";
            break;
        default:
            assert(0);
            break;
        }
        if (offering.find("representation_info") != offering.end()){
            auto d = offering["representation_info"][f];
            LOG_INFO(_ctx, "segment_duration","duration", (elv_fp)d);
            return (elv_fp)d;
        }
        return 0;
    }
    int segment_num_int(){
        if (p.seg_num == "init")
            return 1;
        return std::stoi(p.seg_num);
    }
    std::pair<elv_fp, elv_fp> segment_timeline_points(){
        elv_fp d = segment_duration();
        int i = segment_num_int(); /* if 'init' use '1' */
        elv_fp start_secs = (i - 1) * d;
        elv_fp end_secs = start_secs + d;
        return std::pair<elv_fp, elv_fp>(start_secs, end_secs);
    }
    json segment_sequence(){
        // Extract resources for the type
        std::string json_seq_str;
        switch (p.avtype) {
        case 'v':
            json_seq_str = "video_sequence";
            break;
        case 'a':
            json_seq_str = "audio_sequence";
            break;
        }
        auto seq = offering["offering"][json_seq_str];
        return seq;
    }
    std::vector<avpart> resources_for_timeline_slice(elv_fp start_secs, elv_fp end_secs){
        std::vector<avpart> result;
        auto segseq = segment_sequence();
        auto resources = segseq["resources"];

        LOG_INFO(_ctx, "resources_for_timeline_slice", "start", start_secs , " end" , end_secs , " res" , resources);
        for(auto& r: resources) {
            if (resource_overlaps(r, start_secs, end_secs)) {
                avpart part;
                elv_fp res_entry = r["entry_point"];
                elv_fp res_tl_start = r["timeline_start"];
                elv_fp res_tl_end = r["timeline_end"];
                elv_fp intersection_tl_start = (res_tl_start > start_secs) ? res_tl_start : start_secs;
                elv_fp intersection_tl_end = (res_tl_end > end_secs) ? end_secs : res_tl_end;
                elv_fp tl_to_asset_convert = res_entry - res_tl_start;


                part.start_secs = intersection_tl_start + tl_to_asset_convert;
                part.end_secs = intersection_tl_end + tl_to_asset_convert;
                part.phash = r["phash"];
                part.temp_file_name = std::string(base_temp_template) + part.phash;
                result.push_back(part);
            }
            // FIXME: if resources are sorted by time, exit here if we past 'end_secs' already
        }
        return result;
    }
    bool resource_overlaps(json r, elv_fp start_secs, elv_fp end_secs){
        auto r_start = r["timeline_start"];
        auto r_end = r["timeline_end"];
        return (r_start < end_secs && r_end > start_secs);
    }

private:
    std::string url;
    std::string offering_json_str;
    std::string watermark_json_str;
    json offering;
    json watermark;
public:
    dash_params p;
    BitCodeCallContext* _ctx;
};
