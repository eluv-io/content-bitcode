#pragma once
#include <sys/stat.h>
#include <string>
#include <fstream>
#include <streambuf>
#include <iostream>
#include <nlohmann/json.hpp>
#include "el_constants.h"
#include "el_cgo_interface.h"
#include "utils.h"
#include "bitcode_context.h"

using namespace ffmpeg_elv_constants;
using namespace elv_context;
using nlohmann::json;

template <bool GoGenerate = false>
class CommandlineGenerator{
public:
    std::string dummyName;
    std::string dummySeg;
    BitCodeCallContext* _ctx;


    CommandlineGenerator(BitCodeCallContext* ctx, DashParams& p) : _params(p){
        _ctx = ctx;
     }

    std::vector<std::string> Flatten(){
        std::vector<std::string> ret = GetArgsPreamble();
        AppendTo(ret, GetArgsInputList());
        AppendTo(ret, GetArgsMap());
        AppendTo(ret, GetArgsCodec());
        AppendTo(ret, GetArgsBitrate());
        AppendTo(ret, GetArgsFilter());
        AppendTo(ret, GetArgsKeyint());
        AppendTo(ret, GetArgsOutput());
        return ret;
    }

    std::string GetMediaSegmentFilename(){
        if (!GoGenerate){
            if (_params.seg_num != "init"){
                char filename[1024];
                GetSegmentFilename(filename);
                return std::string(filename);
            }
            else{
                char buf[1024];
                char bufInner[128];
                sprintf(bufInner, "%s", dummy_filename_template);
                mktemp(bufInner);
                sprintf(buf, "%s.%s", bufInner, (_params.avtype == 'a') ? "m4a" : "m4v");
                dummySeg = std::string(buf);
                return dummySeg;
            }
            return "";
        }else{
            return std::string("%MEDIA%");
        }
    }

    std::string GetInitSegmentFilename(){
        if (!GoGenerate){
            if (_params.seg_num == "init"){
                char filename[1024];
                GetSegmentFilename(filename);
                return std::string(filename);
            }
            else{
                char buf[1024];
                char bufInner[128];
                sprintf(bufInner, "%s", dummy_filename_template);
                mktemp(bufInner);
                sprintf(buf, "%s.%s", bufInner, (_params.avtype == 'a') ? "m4a" : "m4v");
                dummySeg = std::string(buf);
                return dummySeg;
            }
            return "";
        }else{
            return std::string("%INIT%");
        }
    }

    bool IsInit(){
        return _params.seg_num == "init";
    }

    DashParams& _params;

    void AppendTo(std::vector<std::string>& to, std::vector<std::string> from){
        to.insert(to.end(), from.begin(), from.end());
    }

    std::vector<std::string> GetArgsPreamble(){
        std::vector<std::string> ret = {"-hide_banner", "-nostats", "-loglevel",  "error", "-y"};
        return ret;
    }

    std::vector<std::string> GetArgsDashFixed(){
        std::vector<std::string> ret = {"-f", "dash", "-use_template", "0",  "-use_timeline", "0", "-remove_at_exit", "0"};
        return ret;
    }


    std::vector<std::string> GetArgsInputList(){
        std::vector<std::string>  ret;
        for (int i = 0; i < _params.part_list.size(); i++) {
            ret.push_back("-ss");
            char buf[128];
            sprintf(buf, input_list_start_precision, _params.part_list[i].start_secs);
            ret.push_back(std::string(buf));
            ret.push_back("-to");
            sprintf(buf, input_list_end_precision,_params.part_list[i].end_secs);
            ret.push_back(std::string(buf));
            if (!GoGenerate){
                ret.push_back("-i");
                ret.push_back(_params.part_list[i].temp_file_name);
            }else{
                ret.push_back("-i");
                auto inputFileTemplate = std::string("%INPUTFILE") + std::string("_") + std::to_string(i) + std::string("%");
                ret.push_back(inputFileTemplate);
            }
        }
        return ret;
    }

    std::vector<std::string> GetArgsMap(){
        std::vector<std::string>  ret;
        ret.push_back("-map");
        if (_params.part_list.size() == 1){ //simple
            char buf[1024];
            sprintf(buf, "0:%c", _params.avtype);
            ret.push_back(buf);
        }else{
            char buf[1024];
            sprintf(buf, "[%c]", _params.avtype);
            ret.push_back(buf);
        }
        return ret;
    }

    std::vector<std::string> GetArgsCodec(){
        std::vector<std::string>  ret;
        switch (_params.avtype){
            case 'a':
                ret.push_back("-acodec");
                ret.push_back("aac");
                break;
            case 'v':
                ret.push_back("-vcodec");
#if defined (__APPLE__)
                ret.push_back("h264_videotoolbox");
#elif defined (__linux__)
                ret.push_back("libx264");
#else
                ret.push_back("libx264");
#endif
                break;
            default:
                break;
        };
        return ret;
    }

    std::vector<std::string> GetArgsBitrate(){
        std::vector<std::string>  ret;
        char buf[1024];
        sprintf(buf, "-b:%c", _params.avtype);
        ret.push_back(buf);
        ret.push_back(std::to_string(_params.rep.bitrate));
        return ret;
    }

    void GetBracketedN(std::string& in, int n){
        for (int i=0; i<(n+1); i++){
            char buf[128];
            sprintf(buf, "[%d]", i);
            in += buf;
        }
    }

    void GetSegmentFilename(char* buf){
        //"#{@language}-#{@rep[:name]}@#{bitrate}-#{@segment}.#{segment_file_ext}"
        sprintf(buf, segment_filename_template,
                        _params.language.c_str(),
                        _params.rep_name.c_str(),
                        _params.rep.bitrate,
                        _params.seg_num.c_str(),
                        (_params.avtype == 'v') ? "m4v" : "m4a" );
    }

    bool file_exists (const std::string& name) {
        struct stat buffer;
        return (stat (name.c_str(), &buffer) == 0);
    }

    std::string GetFontPath(){
        std::string temp_font_path = std::string(base_temp_path) + std::string("el-font-file.ttf");
        if (!file_exists(temp_font_path)){
            std::string str = base64_decode(ffmpeg_elv_fontspace::elv_mono_font_base64);
            std::ofstream out(temp_font_path);
            out << str;
            out.close();
        }
        return temp_font_path;
    }


    std::string EscapeChar(std::string& watermark, char ch){
        std::string& temp = watermark;
        std::size_t pos = 0;
        while (pos != std::string::npos){
            pos = temp.find(ch, pos);
            if (pos == std::string::npos)
                return watermark;
            temp = temp.insert(pos, std::string(1, '\\'));
            pos+= 2;
        }
        return temp;
    }

    std::string EscapeChars(std::string& watermark){
        watermark = EscapeChar(watermark, '\\');
        watermark = EscapeChar(watermark, '\'');
        watermark = EscapeChar(watermark, '\"');
        return watermark;
    }

    const char* GetStandardReplacement(std::string& str){
        if (str == "User.Address")
            return _ctx->GetCallingUser();
        else if (str == "Node.Address")
            return "NOHASH";
        else if (str == "Library.Id")
            return "NOLIB";
        else if (str == "Content.Id")
            return "Some ContentID"; // TODO this needs to be passed in like qhash & qlibid
        else
            return "<ERROR_UNKNOWN>";
    }

    std::string ExpandWatermark(std::string& watermark){
        // User.Address blockchain addresses
        // Node.Address blockchain addresses
        // Library.Id blockchain addresses
        // Content.Id ??
        const char* replacement_begin = "{{";
        const char* replacement_end = "}}";
        static std::vector<std::string> replacements = {"User.Address","Node.Address","Library.Id","Content.Id"};
        size_t block_begin = 0;
        std::string output;
        int found_count = 0;

        auto found = watermark.find(replacement_begin);  // no replacements
        if (found == std::string::npos)
            return watermark;

        int foundEnd=0;

        do{
            if (found != std::string::npos){
                 if (found >= block_begin){
                    output += watermark.substr(block_begin, found-block_begin);
                    block_begin = found;
                    foundEnd = watermark.find(replacement_end, found);
                    std::string strReplacement = watermark.substr(block_begin+strlen(replacement_end), (foundEnd-(block_begin+strlen(replacement_end))));
                    auto iFound = find(replacements.begin(), replacements.end(), strReplacement);
                    if (iFound != replacements.end()){
                        output += GetStandardReplacement(*iFound);
                        found_count++;
                    }
                    else{
                        output += strReplacement + " Not found in dictionary";
                        // parse is really messed up here just return
                        return output;
                    }
                    block_begin = foundEnd + strlen(replacement_end);
                }
            }
            found = watermark.find(replacement_begin, foundEnd);
            if (found == std::string::npos){// end
                int last_tag_pos = foundEnd + strlen(replacement_end);
                output += watermark.substr(last_tag_pos, watermark.length() - last_tag_pos);
            }
        }while(found != std::string::npos) ;
        return output;
    }

    std::string GetManifestPath(){
        if (!GoGenerate){
            char buf[1024];
            strcpy(buf, dummy_filename_template);
            LOG_INFO(_ctx, "tempfile=", mktemp(buf));
            dummyName = base_temp_path;
            dummyName += std::string(buf);
            dummyName += ".mpd";
            LOG_INFO(_ctx, dummyName);
            return dummyName;
        }else{
            return std::string("%MANIFEST%");
        }
    }

    std::string GetBaseFilterLocation(){
        //eg pos_x = (w-tw)/2
        //   pos_y = (h/2)
        char buf[4096];
        std::string posX = _params.watermark["pos_x"];
        std::string posY = _params.watermark["pos_y"];

        sprintf(buf, "x=%s: y=%s", posX.c_str(), posY.c_str());
        return std::string(buf);
    }

    std::string GetFontSize(){
        char buf[2048];
        // eg fontsize=(h/15)
        std::string fontSize = _params.watermark["font_size"];
        sprintf(buf, "fontsize=%s",fontSize.c_str());
        return std::string(buf);
    }

    std::string GetFontColor(){
        auto it = _params.watermark.find("font_color");
        if (it != _params.watermark.end())
            return *it;
        else
            return "yellow";
    }


    std::string GetFilterWatermark(){
        if (_params.watermark.empty())
            return "";
        else{
            char buf[4096];
            auto it = _params.watermark.find("text");
            if (it != _params.watermark.end()){
                std::string watermark_text = *it;
                std::string expandedWatermark = ExpandWatermark(watermark_text);
                sprintf(buf, base_filter_watermark, GetFontPath().c_str(), EscapeChars(expandedWatermark).c_str(),
                                                    GetBaseFilterLocation().c_str(), GetFontSize().c_str(), GetFontColor().c_str());
                return std::string(", ") + buf;
            }
            else{
                return "";
            }
        }
    }




    std::vector<std::string> GetArgsFilter(){
        std::vector<std::string>  ret;
        int n = _params.part_list.size();
        if (n == 1){ //simple
            if (_params.avtype == 'v'){
                ret.push_back("-vf");
                char buf[1024];
                sprintf(buf,base_scale_template, _params.rep.width, _params.rep.height, GetFilterWatermark().c_str());
                ret.push_back(buf);
            }// Audio gets NOTHING
        }else{
            ret.push_back("-filter_complex");
            char middle[1024];
            char scale[1024];
            if (_params.avtype == 'v'){
                sprintf(scale,base_scale_template, _params.rep.width, _params.rep.height, GetFilterWatermark().c_str());
                sprintf(middle, "[cv];[cv]%s",scale);
            }else{
                strcpy(middle,":v=0:a=1");
            }
            std::string bracketed;
            GetBracketedN(bracketed, n-1);
            //concat=n=#{n}#{middle}[#{type_id}]
            bracketed += "concat=n=";
            bracketed += std::to_string(n);
            bracketed += middle;
            char buf[128];
            sprintf(buf, "[%c]", _params.avtype);
            bracketed += buf;
            ret.push_back(bracketed);
        }
        return ret;
    }
    std::vector<std::string> GetArgsKeyint(){
        std::vector<std::string>  ret;
            if (_params.avtype == 'v'){
            ret.push_back("-x264opts");
            char buf[1024];
            sprintf(buf,"keyint=%d:min-keyint=%d:scenecut=-1", _params.num_frames,  _params.num_frames);
            ret.push_back(buf);
        }
        return ret;
    }

    std::vector<std::string> GetArgsOutput(){
        std::vector<std::string>  ret;
        float duration = (float)_params.seg_duration_secs + (_params.avtype == 'a' ? audio_segment_padding_seconds : video_segment_padding_seconds);
        ret.push_back("-seg_duration");
        char buf[128];
        sprintf(buf,"%f", duration);
        ret.push_back(std::string(buf));
        AppendTo(ret, GetArgsDashFixed());
        ret.push_back("-init_seg_name");
        ret.push_back(GetInitSegmentFilename());
        ret.push_back("-media_seg_name");
        ret.push_back(GetMediaSegmentFilename());
        ret.push_back(GetManifestPath());
        return ret;
    }
};
