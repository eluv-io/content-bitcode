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
#include "linux-htonl.h"
#include <nlohmann/json.hpp>
#include "eluvio/dasher.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"
#include "eluvio/bitcode_context.h"

using namespace std;

class FFmpegFixup{
private:

    //static const uint64_t video_basetime_const = 144144;
    //static const uint64_t audio_basetime_const = 288768;
    /*
    * Finds section of 'tyoe' in segment and returns the beginning of the section.
    *
    * Section format:
    *    |   size  |   type    |   data    |
    *       4 byte    4 chars     size - 8
    */
    static int box_find(BitCodeCallContext* ctx, char *seg, int segsz, const char *type) {
        int pos = 0;
        uint32_t boxsz;

        LOG_DEBUG(ctx, "box_find", "type", type);
        while (pos + 8 < segsz ) {
            memcpy(&boxsz, seg + pos, 4);
            boxsz = host_network_byte_order::ntohl32(boxsz);
            LOG_DEBUG(ctx, "box_find", "boxsz", boxsz,  "boxtype", seg + pos + 4);
            if (boxsz == 0) {
                LOG_ERROR(ctx, "box sz is 0");
                break;
            }
            if (!strncmp(seg + pos +4, type, 4)) {
                LOG_INFO(ctx, "found box", "type", type, "pos", pos);
                return pos;
            } else {
                pos += boxsz;
            }
        }
        return -1;
    }

    /*
    * Finds section of 'type' inside the section starting at 'seg'
    * (i.e. don't go past the size of this top section)
    */
    static int
    box_find_inside(BitCodeCallContext* ctx,  char *seg, int segsz, const char *type) {
        uint32_t boxsz;

        memcpy(&boxsz, seg, 4);
        boxsz = host_network_byte_order::ntohl32(boxsz);
        LOG_INFO(ctx, "find inside", "boxsz",boxsz, "boxtype", seg + 4);
        if (boxsz == 0) {
            LOG_ERROR(ctx, "sz is 0");
            return -1;
        }
        return box_find(ctx, seg + 8, boxsz - 8, type);
    }
    /*
    * Find 3 elements:
    * - sidx.earlist_presentation_time  8 bytes
    * - moof.mfhd.sequence_number       4 bytes
    * - moof.traf.tfdt.base             8 bytes
    * If argument 'stamp' is true, write the elements in the segment.
    * If false, read the elements from the segment as return arguments.
    */
    static int
    find(BitCodeCallContext* ctx,  char *seg, int segsz, int stamp,
        uint8_t sidx_ept[8], uint8_t mfhd_seqno[4], uint8_t tfdt_base[8]) {

        LOG_DEBUG(ctx, "find", "segsz", segsz, "stamp", stamp);

        /* Find sidx.earlist_presentation_time */
        int pos_sidx = box_find(ctx, seg, segsz, "sidx");
        LOG_DEBUG(ctx, "find", "sidx pos", pos_sidx);

        if (stamp) {
            memcpy(seg + pos_sidx + 20, sidx_ept, 8);
        } else {
            memcpy(sidx_ept, seg + pos_sidx + 20, 8);
            LOG_DEBUG(ctx, "find", "sidx_ept", "0", sidx_ept[0], "1", sidx_ept[1], "6", sidx_ept[6], "7", sidx_ept[7]);
        }

        /* Find moof.mfhd.sequence_number */
        int pos_moof = box_find(ctx, seg, segsz, "moof");
        LOG_DEBUG(ctx, "find", "moof pos", pos_moof);
        int pos_mfhd_rel = box_find_inside(ctx, seg + pos_moof, segsz - pos_moof, "mfhd");
        int pos_mfhd = pos_moof + 8 + pos_mfhd_rel;
        LOG_DEBUG(ctx, "find", "mfhd pos", pos_mfhd);

        if (stamp) {
            memcpy(seg + pos_mfhd + 12, mfhd_seqno, 4);
        } else {
            memcpy(mfhd_seqno, seg + pos_mfhd + 12, 4);
            LOG_DEBUG(ctx, "find", "mfhd_seqno", "0", mfhd_seqno[0], "1", mfhd_seqno[1], "2", mfhd_seqno[2], "3", mfhd_seqno[3]);
        }

        /* Find moof.traf.tfdt.base */
        int pos_traf_rel = box_find_inside(ctx, seg + pos_moof, segsz - pos_moof, "traf");
        int pos_traf = pos_moof + 8 + pos_traf_rel;
        LOG_DEBUG(ctx, "find", "traf pos", pos_traf);
        int pos_tfdt_rel = box_find_inside(ctx, seg + pos_traf, segsz - pos_traf, "tfdt");
        int pos_tfdt = pos_traf + 8 + pos_tfdt_rel;
        LOG_DEBUG(ctx, "find", "tfdt pos", pos_tfdt);

        if (stamp) {
            memcpy(seg + pos_tfdt + 12, tfdt_base, 8);
        } else {
            memcpy(tfdt_base, seg + pos_tfdt + 12, 8);
            LOG_DEBUG(ctx, "find", "tfdt_base", "0", tfdt_base[0], "1",
            tfdt_base[1], "6", tfdt_base[6], "7", tfdt_base[7]);
        }

        return 0;
    }

public:
    static int
    ffmpegFixup(BitCodeCallContext* ctx,  char *seg, int segsz, uint32_t new_seqno, const char *contenttype, int chunks_or_frames) {

        if (new_seqno == 0) return 0; // Initializer segment

        // if (strstr(contenttype, "video"))
        //     basetime_const = video_basetime_const;
        // else
        //     basetime_const = audio_basetime_const;

        LOG_INFO(ctx, "ffmpegFixup", "contenttype", contenttype, "chunks_or_frames", chunks_or_frames);
        //uint64_t new_basetime = (uint64_t)(decode_time_duration_secs * (new_seqno - 1));
        // get default sample duration
        // Find box at top level of type 'moof' (movie fragment)
        int pos_moof = box_find(ctx, seg, segsz, "moof");
        int pos_traf_rel = box_find_inside(ctx, seg + pos_moof, segsz - pos_moof, "traf");
        int pos_traf = pos_moof + 8 + pos_traf_rel;
        LOG_DEBUG(ctx, "ffmpegFixup", "trafpos", pos_traf);
        int pos_tfhd_rel = box_find_inside(ctx, seg + pos_traf, segsz - pos_traf, "tfhd");
        int pos_tfhd = pos_traf + 8 + pos_tfhd_rel;
        LOG_DEBUG(ctx, "ffmpegFixup", "tfhd pos", pos_tfhd);
        uint32_t default_sample_duration;
        memcpy(&default_sample_duration, seg + pos_tfhd + 16, 4);
        default_sample_duration = host_network_byte_order::ntohl32(default_sample_duration);
        LOG_DEBUG(ctx, "ffmpegFixup", "default_sample_duration", default_sample_duration);


        uint64_t new_basetime = (uint64_t)(default_sample_duration * (new_seqno - 1) * chunks_or_frames);

        // Find box at top level of type 'moof' (movie fragment)
        pos_moof = box_find(ctx, seg, segsz, "moof");
        LOG_DEBUG(ctx, "ffmpegFixup", "moof pos", pos_moof);
        int pos_mfhd_rel = box_find_inside(ctx, seg + pos_moof, segsz - pos_moof, "mfhd");
        int pos_mfhd = pos_moof + 8 + pos_mfhd_rel;
        LOG_DEBUG(ctx, "ffmpegFixup", "mfhd pos", pos_mfhd);

        uint32_t old_seqno;
        memcpy(&old_seqno, seg + pos_mfhd + 12, 4);
        old_seqno = host_network_byte_order::ntohl32(old_seqno);
        LOG_DEBUG(ctx, "ffmpegFixup", "old seqno", old_seqno);
        if (old_seqno != 1) {
            LOG_ERROR(ctx, "old_seqno must be 1", "old seqno", old_seqno);
            return -1;
        }
        new_seqno = host_network_byte_order::htonl32(new_seqno);
        memcpy(seg + pos_mfhd + 12, &new_seqno, 4);

        // Find box at top level of type 'traf' (track fragment)
        pos_traf_rel = box_find_inside(ctx, seg + pos_moof, segsz - pos_moof, "traf");
        pos_traf = pos_moof + 8 + pos_traf_rel;
        LOG_DEBUG(ctx, "ffmpegFixup", "traf pos", pos_traf);
        int pos_tfdt_rel = box_find_inside(ctx, seg + pos_traf, segsz - pos_traf, "tfdt");
        int pos_tfdt = pos_traf + 8 + pos_tfdt_rel;
        LOG_DEBUG(ctx, "ffmpegFixup", "tfdt pos", pos_tfdt);

        uint64_t old_basetime;
        memcpy(&old_basetime, seg + pos_tfdt + 12, 8);
        old_basetime = host_network_byte_order::ntohl32(old_basetime);
        LOG_DEBUG(ctx, "ffmpegFixup", "old basetime", (int)old_basetime);
        if (old_basetime != 0) {
            LOG_ERROR(ctx, "old basetime must be 0", "old base time", (int)old_basetime);
            return -1;
        }

        new_basetime = host_network_byte_order::htnol64(new_basetime);
        LOG_DEBUG(ctx, "ffmpegFixup", "new basetime", (int)new_basetime);
        memcpy(seg + pos_tfdt + 12, &new_basetime, 8);

        return 0;
    }

    static int ffmpegFixupLive(BitCodeCallContext* ctx, char *seg, int segsz, char *origseg, int origsegsz) {
        int rc = 0;

        uint8_t sidx_ept[8];
        uint8_t mfdt_seqno[4];
        uint8_t tfdt_base[8];

        rc = find(ctx, origseg, origsegsz, 0, sidx_ept, mfdt_seqno, tfdt_base);
        if (rc < 0) return rc;

        rc = find(ctx, seg, segsz, 1, sidx_ept, mfdt_seqno, tfdt_base);
        if (rc < 0) return rc;

        return 0;
    }
};

