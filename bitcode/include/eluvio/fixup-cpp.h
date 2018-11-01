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
    static int box_find(char *seg, int segsz, const char *type) {
        int pos = 0;
        uint32_t boxsz;

        printf("find box %s\n", type);
        while (pos + 8 < segsz ) {
            memcpy(&boxsz, seg + pos, 4);
            boxsz = host_network_byte_order::ntohl32(boxsz);
            printf("tryig boxsz=%d boxtype=%.4s\n", boxsz, seg + pos + 4);
            if (boxsz == 0) {
                printf("failure - sz is 0\n");
                break;
            }
            if (!strncmp(seg + pos +4, type, 4)) {
                printf("found box %s pos=%d\n", type, pos);
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
    box_find_inside(char *seg, int segsz, const char *type) {
        uint32_t boxsz;

        memcpy(&boxsz, seg, 4);
        boxsz = host_network_byte_order::ntohl32(boxsz);
        printf("find inside boxsz=%d boxtype=%.4s\n", boxsz, seg + 4);
        if (boxsz == 0) {
            printf("failure - sz is 0\n");
            return -1;
        }
        return box_find(seg + 8, boxsz - 8, type);
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
    find(char *seg, int segsz, int stamp,
        uint8_t sidx_ept[8], uint8_t mfhd_seqno[4], uint8_t tfdt_base[8]) {

        printf("FIXUP live %s\n", stamp ? "stamp" : "find");

        /* Find sidx.earlist_presentation_time */
        int pos_sidx = box_find(seg, segsz, "sidx");
        printf("sidx pos=%d\n", pos_sidx);

        if (stamp) {
            memcpy(seg + pos_sidx + 20, sidx_ept, 8);
        } else {
            memcpy(sidx_ept, seg + pos_sidx + 20, 8);
            printf("sidx_ept %02x-%02x ... %02x-%02x\n",
                sidx_ept[0], sidx_ept[1], sidx_ept[6], sidx_ept[7]);
        }

        /* Find moof.mfhd.sequence_number */
        int pos_moof = box_find(seg, segsz, "moof");
        printf("moof pos=%d\n", pos_moof);
        int pos_mfhd_rel = box_find_inside(seg + pos_moof, segsz - pos_moof, "mfhd");
        int pos_mfhd = pos_moof + 8 + pos_mfhd_rel;
        printf("mfhd pos=%d\n", pos_mfhd);

        if (stamp) {
            memcpy(seg + pos_mfhd + 12, mfhd_seqno, 4);
        } else {
            memcpy(mfhd_seqno, seg + pos_mfhd + 12, 4);
            printf("mfhd_seqno %02x-%02x-%02x-%02x\n",
                mfhd_seqno[0], mfhd_seqno[1], mfhd_seqno[2], mfhd_seqno[3]);
        }

        /* Find moof.traf.tfdt.base */
        int pos_traf_rel = box_find_inside(seg + pos_moof, segsz - pos_moof, "traf");
        int pos_traf = pos_moof + 8 + pos_traf_rel;
        printf("traf pos=%d\n", pos_traf);
        int pos_tfdt_rel = box_find_inside(seg + pos_traf, segsz - pos_traf, "tfdt");
        int pos_tfdt = pos_traf + 8 + pos_tfdt_rel;
        printf("tfdt pos=%d\n", pos_tfdt);

        if (stamp) {
            memcpy(seg + pos_tfdt + 12, tfdt_base, 8);
        } else {
            memcpy(tfdt_base, seg + pos_tfdt + 12, 8);
            printf("tfdt_base %02x-%02x-%02x-%02x\n",
            tfdt_base[0], tfdt_base[1], tfdt_base[6], tfdt_base[7]);
        }

        return 0;
    }

public:
    static int
    ffmpegFixup(char *seg, int segsz, uint32_t new_seqno, const char *contenttype, int chunks_or_frames) {

        if (new_seqno == 0) return 0; // Initializer segment

        // if (strstr(contenttype, "video"))
        //     basetime_const = video_basetime_const;
        // else
        //     basetime_const = audio_basetime_const;

        printf("FIXUP contenttype=%s chunks_or_frames=%d\n", contenttype, chunks_or_frames);
        //uint64_t new_basetime = (uint64_t)(decode_time_duration_secs * (new_seqno - 1));
        // get default sample duration
        // Find box at top level of type 'moof' (movie fragment)
        int pos_moof = box_find(seg, segsz, "moof");
        int pos_traf_rel = box_find_inside(seg + pos_moof, segsz - pos_moof, "traf");
        int pos_traf = pos_moof + 8 + pos_traf_rel;
        printf("traf pos=%d\n", pos_traf);
        int pos_tfhd_rel = box_find_inside(seg + pos_traf, segsz - pos_traf, "tfhd");
        int pos_tfhd = pos_traf + 8 + pos_tfhd_rel;
        printf("tfhd pos=%d\n", pos_tfhd);
        uint32_t default_sample_duration;
        memcpy(&default_sample_duration, seg + pos_tfhd + 16, 4);
        default_sample_duration = host_network_byte_order::ntohl32(default_sample_duration);
        printf("default_sample_duration=%d\n", default_sample_duration);


        uint64_t new_basetime = (uint64_t)(default_sample_duration * (new_seqno - 1) * chunks_or_frames);

        // Find box at top level of type 'moof' (movie fragment)
        pos_moof = box_find(seg, segsz, "moof");
        printf("moof pos=%d\n", pos_moof);
        int pos_mfhd_rel = box_find_inside(seg + pos_moof, segsz - pos_moof, "mfhd");
        int pos_mfhd = pos_moof + 8 + pos_mfhd_rel;
        printf("mfhd pos=%d\n", pos_mfhd);

        uint32_t old_seqno;
        memcpy(&old_seqno, seg + pos_mfhd + 12, 4);
        old_seqno = host_network_byte_order::ntohl32(old_seqno);
        printf("old seqno=%d\n", old_seqno);
        if (old_seqno != 1) {
            printf("failure - old_seqno=%d must be 1\n", old_seqno);
            return -1;
        }
        new_seqno = host_network_byte_order::htonl32(new_seqno);
        memcpy(seg + pos_mfhd + 12, &new_seqno, 4);

        // Find box at top level of type 'traf' (track fragment)
        pos_traf_rel = box_find_inside(seg + pos_moof, segsz - pos_moof, "traf");
        pos_traf = pos_moof + 8 + pos_traf_rel;
        printf("traf pos=%d\n", pos_traf);
        int pos_tfdt_rel = box_find_inside(seg + pos_traf, segsz - pos_traf, "tfdt");
        int pos_tfdt = pos_traf + 8 + pos_tfdt_rel;
        printf("tfdt pos=%d\n", pos_tfdt);

        uint64_t old_basetime;
        memcpy(&old_basetime, seg + pos_tfdt + 12, 8);
        old_basetime = host_network_byte_order::ntohl32(old_basetime);
        printf("old basetime=%d\n", (int)old_basetime);
        if (old_basetime != 0) {
            printf("failure - old basetime=%d must be 0\n", (int)old_basetime);
            return -1;
        }

        new_basetime = host_network_byte_order::htnol64(new_basetime);
        printf("new basetime=%d\n", (int)new_basetime);
        memcpy(seg + pos_tfdt + 12, &new_basetime, 8);

        return 0;
    }

    static int ffmpegFixupLive(char *seg, int segsz, char *origseg, int origsegsz) {
        int rc = 0;

        uint8_t sidx_ept[8];
        uint8_t mfdt_seqno[4];
        uint8_t tfdt_base[8];

        rc = find(origseg, origsegsz, 0, sidx_ept, mfdt_seqno, tfdt_base);
        if (rc < 0) return rc;

        rc = find(seg, segsz, 1, sidx_ept, mfdt_seqno, tfdt_base);
        if (rc < 0) return rc;

        return 0;
    }
};

