#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

using nlohmann::json;



class DashManifest{
private:
    class VideoProps{
    public:
        int width;
        int height;
        int bitrate;
        std::string name;
    };

    class AudioProps{
    public:
        int bitrate;
        std::string name;
    };
    typedef std::vector<VideoProps> video_props_class;
    typedef std::vector<AudioProps> audio_props_class;

    class VideoAttributes{
    public:
        video_props_class _props;
        int video_seg_duration_dash;
        elv_fp video_seg_duration_secs;
    };

    class AudioAttributes{
    public:
        audio_props_class _props;
        int audio_seg_duration_dash;
        elv_fp audio_seg_duration_secs;
    };
    bool GetVideo(){
        json jReps = m_j["representation_info"];
        //json jVideo = jReps["video_reps"];
        json jVideo = m_j["representation_info"];
        for (json::iterator it = jVideo.begin(); it != jVideo.end(); ++it) {

            if (it.key() == "video_reps"){
                std::vector<json> videoResolutions = it.value();
                for (std::vector<json>::iterator it = videoResolutions.begin(); it != videoResolutions.end(); ++it) {
                    VideoProps vp;
                    json jCur = *it;
                    vp.bitrate = jCur["bitrate"];
                    vp.height = jCur["height"];
                    vp.width = jCur["width"];
                    std::string nameLocal = jCur["name"];
                    vp.name = nameLocal;
                    m_va._props.push_back(vp);
                }
            }
        }
        m_va.video_seg_duration_dash = jVideo["video_seg_duration_dash"];
        m_va.video_seg_duration_secs = jVideo["video_seg_duration_secs"];
        return true;
    }

    bool GetAudio(){
        json jAudio = m_j["representation_info"];
        for (json::iterator it = jAudio.begin(); it != jAudio.end(); ++it) {

            if (it.key() == "audio_reps"){
                std::vector<json> audioResolutions = it.value();
                for (std::vector<json>::iterator it = audioResolutions.begin(); it != audioResolutions.end(); ++it) {
                    AudioProps ap;
                    json jCur = *it;
                    ap.bitrate = jCur["bitrate"];
                    std::string nameLocal = jCur["name"];
                    ap.name = nameLocal;
                    m_aa._props.push_back(ap);
                }
            }
        }
        m_aa.audio_seg_duration_dash = jAudio["audio_seg_duration_dash"];
        m_aa.audio_seg_duration_secs = jAudio["audio_seg_duration_secs"];
        return true;
    }

    void CreateAdaptationSet(std::string auth){
        //videoAdaptations.push_back("AdaptationSet", R"({{"bitstreamSwitching" : true}, {"contentType" : "video"} }})");
        std::string adaptationsVideo;
        std::string adaptationsAudio;

        std::string frameRateFraction = m_j["offering"]["frame_rate_fraction"];
        std::string videoSegDuration = std::to_string((int)m_j["representation_info"]["video_seg_duration_dash"]);
        std::string audioSegDuration = std::to_string((int)m_j["representation_info"]["audio_seg_duration_dash"]);


        adaptationsVideo = "<AdaptationSet bitstreamSwitching='true' contentType='video' lang='";
        std::string offering_language = m_j["offering"]["language"];
        adaptationsVideo += offering_language;
        adaptationsVideo += "' segmentAlignment='true'> \n <Role schemeIdUri='urn:mpeg:dash:role:2011' value='main'></Role>\n";

        adaptationsAudio = "<AdaptationSet bitstreamSwitching='true' contentType='audio' segmentAlignment='true'>";
        //iterate on VideoAttributes
        for (video_props_class::iterator it = m_va._props.begin(); it != m_va._props.end(); ++it) {
            adaptationsVideo += "<Representation ";
            adaptationsVideo += " bandwidth='";
            adaptationsVideo += std::to_string((*it).bitrate);
            adaptationsVideo += std::string("'");
            adaptationsVideo += " codecs='avc1.64001f' frameRate='";
            adaptationsVideo += frameRateFraction;
            adaptationsVideo += "' height='";
            adaptationsVideo +=  std::to_string((*it).height);
            adaptationsVideo += "' id='";
            adaptationsVideo += (*it).name;
            adaptationsVideo += std::string("'");
            adaptationsVideo += " mimeType='video/mp4' ";
            adaptationsVideo += " width='";
            adaptationsVideo +=  std::to_string((*it).width);
            adaptationsVideo +=  std::string("'");
            adaptationsVideo += std::string(">\n<SegmentTemplate duration='");
            adaptationsVideo += videoSegDuration;
            char buf[2048];
            std::string lang = m_j["offering"]["language"];
            std::string time_scale = std::to_string((int)m_j["dash_options"]["video_timescale"]);
            if (auth != "")
                sprintf(buf,"' initialization='%s-$RepresentationID$-init.m4v?%s' media='%s-$RepresentationID$-$Number$.m4v?%s' startNumber='1' timescale='%s'></SegmentTemplate>\n", lang.c_str(), auth.c_str(), lang.c_str(), auth.c_str(), time_scale.c_str());
            else
                sprintf(buf,"' initialization='%s-$RepresentationID$-init.m4v' media='%s-$RepresentationID$-$Number$.m4v' startNumber='1' timescale='%s'></SegmentTemplate>\n", lang.c_str(), lang.c_str(), time_scale.c_str());
            adaptationsVideo += buf;
            adaptationsVideo += std::string("\n</Representation>\n");

        }
        adaptationsVideo += std::string("</AdaptationSet>\n");
        for (audio_props_class::iterator it = m_aa._props.begin(); it != m_aa._props.end(); ++it) {
            adaptationsAudio += "<Representation codecs='mp4a.40.2'  mimeType='audio/mp4' audioSamplingRate='48000' ";
            adaptationsAudio += " id='";
            adaptationsAudio += (*it).name;
            adaptationsAudio += std::string("'");
            adaptationsAudio += " bandwidth='";
            adaptationsAudio += std::to_string((*it).bitrate);
            adaptationsAudio += std::string("'><AudioChannelConfiguration schemeIdUri='urn:mpeg:dash:23003:3:audio_channel_configuration:2011' value='2'></AudioChannelConfiguration><SegmentTemplate duration='");
            adaptationsAudio += audioSegDuration;
            char buf[2048];
            std::string lang = m_j["offering"]["language"];
            std::string time_scale = std::to_string((int)m_j["dash_options"]["audio_timescale"]);
            if (auth != "")
                sprintf(buf,"' initialization='%s-$RepresentationID$-init.m4a?%s' media='%s-$RepresentationID$-$Number$.m4a?%s' startNumber='1' timescale='%s'></SegmentTemplate>\n", lang.c_str(), auth.c_str(), lang.c_str(), auth.c_str(), time_scale.c_str());
            else
                sprintf(buf,"' initialization='%s-$RepresentationID$-init.m4a' media='%s-$RepresentationID$-$Number$.m4a' startNumber='1' timescale='%s'></SegmentTemplate>\n", lang.c_str(), lang.c_str(), time_scale.c_str());
            adaptationsAudio += buf;
            adaptationsAudio += std::string("\n</Representation>\n");
        }
        adaptationsAudio += std::string("</AdaptationSet>\n");
        m_adaptationSet =  "\n<ProgramInformation></ProgramInformation>\n<Period start='PT0.0S'>\n";
        m_adaptationSet += adaptationsVideo + adaptationsAudio;

    }
// Properties
    json&                   m_j;
    VideoAttributes         m_va;
    AudioAttributes         m_aa;
    std::string             m_adaptationSet;
    std::string             m_manifestString;

public:
     DashManifest(json& j):m_j(j){}
     std::string& Create(std::string& auth){
        std::string headerString =  R"(<?xml version="1.0" encoding="UTF-8"?><MPD mediaPresentationDuration=')";
        std::string offering_prog_len = m_j["offering"]["program_length"];
        headerString += offering_prog_len;
        headerString += R"(' minBufferTime='PT12.0S' profiles='urn:mpeg:dash:profile:isoff-ondemand:2011' type='static' xmlns:xlink='http://www.w3.org/1999/xlink' xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' xmlns='urn:mpeg:dash:schema:mpd:2011' xsi:schemaLocation='urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd'>)";

        GetVideo();
        GetAudio();
        CreateAdaptationSet(auth);

        m_manifestString = headerString + m_adaptationSet;
        m_manifestString += "\n</Period></MPD>\n";

        return m_manifestString;
    }

};
