/*
 * dasher.cpp
 *
 * Compile for unit test:
 * clang++ -DUNIT_TEST -I json/include/ -std=c++11 -fno-exceptions -Wall -o dash_test dash.cpp
 */

#include <vector>
#include <string>
#include <iostream>
#include <regex>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct avpart {
    float start_secs;
    float end_secs;
    std::string phash;
    std::string temp_file_name;

    void dump() {
        std::cout << "  " << start_secs << "-" << end_secs << " part: " <<
            phash << " tmp: " << temp_file_name << std::endl;
    }
};

struct representation {
    std::string rep_name;
    int height;
    int width;
    int bitrate;

    void dump() {
        std::cout << "  " << rep_name << " " << height << " " << width << " " << bitrate << std::endl;
    }
};

struct dash_params {
    char avtype; /* TODO enum */
    std::string language;
    std::string rep_name;
    std::string seg_num;
    float seg_duration_secs;
    float decode_time_duration_secs;
    int num_frames; /* only for video */
    representation rep;
    std::vector<avpart> part_list;

    void dump() {
        std::cout << "avtype: " << avtype << std::endl;
        std::cout << "language: " << language << std::endl;
        std::cout << "rep_name: " << rep_name << std::endl;
        std::cout << "seg_num: " << seg_num << std::endl;
        std::cout << "seg_duration_secs: " << (float)seg_duration_secs << std::endl;
        std::cout << "decode_time_duration_secs: " << (float)decode_time_duration_secs << std::endl;
        std::cout << "num_frames: "<< num_frames <<  std::endl;
        std::cout << "rep: " << std::endl;
        rep.dump();
        std::cout << "part_list: " << std::endl;
        for (auto& i: part_list) {
            i.dump();
        }
    }
};

struct dash_params
mock_dash_params_en_360p_init_or_1() {
    struct dash_params p;
    p.avtype = 'v';
    p.language = "en";
    p.rep_name = "360p";

    p.rep.height = 360;
    p.rep.width = 640;
    p.rep.bitrate = 960000;
    p.rep.rep_name = "360p";

    p.seg_num = "init"; /* Change to "1" for segment 1 */
    p.seg_duration_secs = 6.006;
    p.decode_time_duration_secs = 144144.0;
    p.num_frames = 144;

    struct avpart part;
    part.start_secs = 0;
    part.end_secs = 6.006;
    part.phash = "hqp_QmUEawRQpSa8jihyWv2a2a5EsmU8zQezfL7p9NDH5cABHd";
    p.part_list.push_back(part);

    return p;
}

class Dasher {

public:
    Dasher(std::string url, std::string offering_json_str, std::string watermark = std::string(""));
    void initialize();

private:
    void gen_part_list();
    void gen_rep_params();
    void gen_params();
    void gen_url_params();

    float decode_time_duration_secs();
    float segment_duration();
    int segment_num_int();
    std::pair<float, float> segment_timeline_points();
    json segment_sequence();
    std::vector<avpart> resources_for_timeline_slice(float start_secs, float end_secs);
    bool resource_overlaps(json r, float start_secs, float end_secs);

private:
    std::string url;
    std::string offering_json_str;
    std::string offering_json_watermark_str;
    json offering;
    json offering_watermark;
    dash_params p;
};

Dasher::Dasher(std::string url, std::string offering_json_str, std::string offering_watermark_str) :
                url(url),offering_json_str(offering_json_str), offering_json_watermark_str(offering_watermark_str){
    offering = json::parse(offering_json_str);
    offering_watermark = json::parse(offering_watermark_str);

}

void
Dasher::initialize()
{
    gen_url_params();
    gen_part_list();
    gen_rep_params();
    gen_params();

    p.dump();
}

void
Dasher::gen_url_params()
{
    auto u = url;
    std::replace(u.begin(), u.end(), '-', ' ');
    std::replace(u.begin(), u.end(), '.', ' ');

    char l[128];
    char r[128];
    char s[128];
    char x[128];
    std::sscanf(u.c_str(), "%s %s %s %s", l, r, s, x);
    std::cout << "DBG URL lang:" << l << " rep:" << r << " seg:" << s << " ext:" << x << std::endl;

    if (!strcmp(x, "m4v"))
        p.avtype = 'v';
    else if (!strcmp(x, "m4a"))
        p.avtype = 'a';
    p.language = l;
    p.rep_name = r;
    p.seg_num = s;
}

void
Dasher::gen_part_list()
{
    avpart part;

    auto start_end = segment_timeline_points();
    std::vector<avpart> avpart_list = resources_for_timeline_slice(start_end.first, start_end.second);
    p.part_list = avpart_list;
}

void
Dasher::gen_rep_params()
{
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
        std::cout << "DBG -- r: " << r << std::endl;
        if (r["name"] == p.rep_name) {
            p.rep.rep_name = r["name"];
            p.rep.height = r["height"];
            p.rep.width = r["width"];
            p.rep.bitrate = r["bitrate"];
        }
    }
}

void
Dasher::gen_params()
{
    if (p.avtype == 'v')
        p.num_frames = offering["representation_info"]["video_frames"];
    p.seg_duration_secs = segment_duration();
    p.decode_time_duration_secs = decode_time_duration_secs();
}

float
Dasher::decode_time_duration_secs()
{
    auto seg_seq = segment_sequence();
    float sidx_timescale = seg_seq["sidx_timescale"];
    return sidx_timescale * segment_duration();
}

int
Dasher::segment_num_int()
{
    if (p.seg_num == "init")
        return 1;
    return std::stoi(p.seg_num);
}

float
Dasher::segment_duration()
{
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
    auto d = offering["representation_info"][f];
    std::cout << "DBG segment_duration: " << (float)d << std::endl;
    return (float)d;
}

std::pair<float, float>
Dasher::segment_timeline_points()
{
    float d = segment_duration();
    int i = segment_num_int(); /* if 'init' use '1' */
    float start_secs = (i - 1) * d;
    float end_secs = start_secs + d;
    return std::pair<float, float>(start_secs, end_secs);
}

json
Dasher::segment_sequence()
{
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

bool
Dasher::resource_overlaps(json r, float start_secs, float end_secs)
{
    auto r_start = r["timeline_start"];
    auto r_end = r["timeline_end"];
    return (r_start < end_secs && r_end > start_secs);
}

std::vector<avpart>
Dasher::resources_for_timeline_slice(
    float start_secs,
    float end_secs)
{
    std::vector<avpart> result;
    auto segseq = segment_sequence();
    auto resources = segseq["resources"];

    std::cout << "DBG res_for_time start:" << start_secs << " end:" << end_secs << " res:" << resources << std::endl;
    for(auto& r: resources) {
        if (resource_overlaps(r, start_secs, end_secs)) {
            avpart part;
            part.start_secs = r["timeline_start"];
            part.end_secs = r["timeline_end"];
            part.phash = r["phash"];
            part.temp_file_name = "tmp." + std::to_string(rand()) + "." + part.phash;
            result.push_back(part);
        }
        // FIXME: if resources are sorted by time, exit here if we past 'end_secs' already
    }
    return result;
}

class FFmpegDasher {

    FFmpegDasher(dash_params p);

private:
    dash_params p;
};

FFmpegDasher::FFmpegDasher(
    dash_params p) :
p(p)
{
}

#ifdef UNIT_TEST

std::string url_en = "en-360p-init.m4v";

std::string offering_en = R"({"representation_info":{"audio_chunks":282,"audio_reps":[{"name":"stereo","bitrate":128000}],"audio_seg_duration_dash":6016000.0,"audio_seg_duration_secs":6.016,"video_frames":144,"video_reps":[{"width":640,"name":"360p","height":360,"bitrate":960000},{"width":853,"name":"480p","height":480,"bitrate":1280000},{"width":1280,"name":"720p","height":720,"bitrate":2560000},{"width":1920,"name":"1080p","height":1080,"bitrate":5120000}],"video_seg_duration_dash":6006000.0,"video_seg_duration_secs":6.006},"offering":{"language":"en","program_length":"PT0H25M52.22S","audio_sequence":{"resources":[{"entry_point":0,"source_duration":14316302,"timeline_end":298.25629166666664,"timeline_start":0,"track_file_id":"2173897f-404c-452e-af1a-97d607bfc5af","phash":"hqp_QmVJ5xPwDtXC9oRLDr125tyfgRVgGGVzsrMTdDz7kSvqdu","path":"/MEDIA/The Mummy IMF Short POC test_File_2.mxf"},{"entry_point":0,"source_duration":60190130,"timeline_end":1552.2173333333333,"timeline_start":298.25629166666664,"track_file_id":"a61f4a23-6c97-47ad-8193-75437394c5b6","phash":"hqp_QmT9ZyTkgn9RGGgEczteotquGxLUhYqadGKnaePJfALZUM","path":"/MEDIA/The Mummy IMF Short POC test_File_2_002.mxf"}],"sidx_timescale":48000},"video_sequence":{"resources":[{"entry_point":0,"source_duration":37216,"timeline_end":1552.2173333333333,"timeline_start":0,"track_file_id":"5ce0db2c-fca4-448e-9962-c47b5097df03","phash":"hqp_QmUEawRQpSa8jihyWv2a2a5EsmU8zQezfL7p9NDH5cABHd", "path":"/MEDIA/The Mummy IMF Short POC test.mxf"}],"sidx_timescale":24000},"audio_sample_rate":48000,"frame_rate_fraction":"24000/1001"},"dash_options":{"audio_timescale":1000000,"video_timescale":1000000}})";

int main(
    int argc,
    char *argv[])
{
    Dasher d(url_en, offering_en);
    d.initialize();

    return 0;
}

#endif

