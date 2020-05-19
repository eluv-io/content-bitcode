#pragma once

typedef float elv_fp;
const char*  eluvio_image_key = "image";
const char* base_download_file = "download.mp4";
const char*  eluvio_public_name_key = "/public/name";
const char*  eluvio_public_image_key = "/public/image";
const char*  eluvio_public_description_key = "/public/description";

namespace ffmpeg_elv_constants{
    const char*  input_list_start_precision = "%.6f";
    const char*  input_list_end_precision = "%.6f";
    const char*  base_temp_path = "./temp/";
    const char*  base_temp_template = "./temp/tmp.";
    const char*  base_filter_watermark = "drawtext=fontfile=%s: text='%s': %s: shadowx=1: shadowy=1: %s:fontcolor=%s";
    const char*  base_filter_location = "x=(w-tw)/2: y=(h/2)";
    const char*  base_scale_template = "scale=%d:%d%s";
    const char*  segment_filename_template = "%s-%s@%d-%s.%s";
    const char*  dummy_filename_template = "dummy-XXXXXX";
    const char*  ffmpeg_font_env_path = "FFMPEG_FONTPATH";
    const elv_fp audio_segment_padding_seconds = 0.01;
    const elv_fp video_segment_padding_seconds = 0.02;
};
