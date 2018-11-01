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
#include "eluvio/dasher.h"
#include "eluvio/cmdlngen.h"
#include "eluvio/argutils.h"
#include "eluvio/fixup-cpp.h"
#include "eluvio/dashmanifest.h"
#include "eluvio/dashsegment.h"
#include "eluvio/utils.h"
#include "eluvio/el_cgo_interface.h"



/*
/home/jan/elv-retail/dist/linux-glibc.2.27/bin/clang++ -g -O0 
-I ELV-TOOLCHAIN/include 
-I content-fabric/bitcode/include 
-Wall -std=c++14  -fno-exceptions -fno-use-cxa-atexit watermark.cpp -o watermark-test
*/


extern "C" char* GetCallingUser(){
    return (char*)"Anonymous";
}


// std::string dummyName;
// std::string dummySeg;
 std::string qlibid = "SOMEREALLYBIGTHINGY_LIB";;
 std::string qhash = "SOMEREALLYBIGTHINGY_HASH";



const char* jsonData = 
R"({
  "id": "iq__cUGcpvRrbjLDNHUN91j2A",
  "hash": "hq__QmUFcsQzrZvqRsJE8YpUPJzx27NfBd99R5BE6C67EaYrFm",
  "type": "hq__QmSyXwFaMsPqZQe2je1apYRwk9et72L3muFDytaDavNypL",
  "meta": {
    "description": "The latest and most impressive version of JPF001",
    "eluv.access.charge": "2.00",
    "eluv.access.type": "paid",
    "eluv.contract_address": "0x8656829eb0f98dc4df3dfeac850318cd7fe76e0e",
    "eluv.image": "hqp_QmXW2sVx8oE8JuaLJauGrKi7diEaoEChqvKowbemnwz6iB",
    "eluv.name": "JPF001",
    "eluv.status": "Approved",
    "eluv.type": "avmaster2000.imf.bc",
    "image": "hqp_QmXW2sVx8oE8JuaLJauGrKi7diEaoEChqvKowbemnwz6iB",
    "languages": "[\"en\"]",
    "name": "JPF001",
    "offering.en": "eyJyZXByZXNlbnRhdGlvbl9pbmZvIjp7ImF1ZGlvX2NodW5rcyI6MjU5LCJhdWRpb19yZXBzIjpbeyJuYW1lIjoic3RlcmVvIiwiYml0cmF0ZSI6MTI4MDAwfV0sImF1ZGlvX3NlZ19kdXJhdGlvbl9kYXNoIjo2MDEzOTY4LjI1Mzk2ODI1NCwiYXVkaW9fc2VnX2R1cmF0aW9uX3NlY3MiOjYuMDEzOTY4MjUzOTY4MjU0LCJ2aWRlb19mcmFtZXMiOjE4MCwidmlkZW9fcmVwcyI6W3sid2lkdGgiOjY0MCwibmFtZSI6IjM2MHAiLCJoZWlnaHQiOjM2MCwiYml0cmF0ZSI6OTYwMDAwfSx7IndpZHRoIjo4NTMsIm5hbWUiOiI0ODBwIiwiaGVpZ2h0Ijo0ODAsImJpdHJhdGUiOjEyODAwMDB9LHsid2lkdGgiOjEyODAsIm5hbWUiOiI3MjBwIiwiaGVpZ2h0Ijo3MjAsImJpdHJhdGUiOjI1NjAwMDB9LHsid2lkdGgiOjE5MjAsIm5hbWUiOiIxMDgwcCIsImhlaWdodCI6MTA4MCwiYml0cmF0ZSI6NTEyMDAwMH1dLCJ2aWRlb19zZWdfZHVyYXRpb25fZGFzaCI6NjAwNjAwMCwidmlkZW9fc2VnX2R1cmF0aW9uX3NlY3MiOjYuMDA2fSwib2ZmZXJpbmciOnsibGFuZ3VhZ2UiOiJlbiIsInByb2dyYW1fbGVuZ3RoIjoiUFQwSDFNMy4wNFMiLCJhdWRpb19zZXF1ZW5jZSI6eyJyZXNvdXJjZXMiOlt7ImVudHJ5X3BvaW50IjowLCJwYXRoIjoiL01FRElBIiwidGltZWxpbmVfZW5kIjo2My4wNDIxNjcsInRpbWVsaW5lX3N0YXJ0IjowLCJwaGFzaCI6ImhxcF9RbWRaUXRialZWS052OHJNTG1takgyVTVyZjh0ZHhQYTRaY3drRjRhS0thNUZOIn1dLCJzaWR4X3RpbWVzY2FsZSI6NDQxMDB9LCJ2aWRlb19zZXF1ZW5jZSI6eyJyZXNvdXJjZXMiOlt7ImVudHJ5X3BvaW50IjowLCJwYXRoIjoiL01FRElBIiwidGltZWxpbmVfZW5kIjo2My4wNDIxNjcsInRpbWVsaW5lX3N0YXJ0IjowLCJwaGFzaCI6ImhxcF9RbWRaUXRialZWS052OHJNTG1takgyVTVyZjh0ZHhQYTRaY3drRjRhS0thNUZOIn1dLCJzaWR4X3RpbWVzY2FsZSI6MzAwMDB9LCJhdWRpb19zYW1wbGVfcmF0ZSI6IjQ0MTAwIiwiZnJhbWVfcmF0ZV9mcmFjdGlvbiI6IjMwMDAwLzEwMDEifSwiZGFzaF9vcHRpb25zIjp7ImF1ZGlvX3RpbWVzY2FsZSI6MTAwMDAwMCwidmlkZW9fdGltZXNjYWxlIjoxMDAwMDAwfX0K",
    "pkg": {
      "Apple_1984_Super_Bowl.mp4": "hqp_QmdZQtbjVVKNv8rMLmmjH2U5rf8tdxPa4ZcwkF4aKKa5FN"
    }
  }
})";

const char* jsonWatermark = 
R"({
    "watermark" : {
        "text"  : "Example text for user {{User.Address}}",
        "pos_x" : "(w-tw)/2", 
        "pos_y" : "h/2", 
        "font_size" : "h/15", 
        "font_color" : "orange", 
        "font_type": "xxx"
    }
})";


std::string expectedResult = R"(, drawtext=fontfile=./temp/el-font-file.ttf: text='Example text for user Anonymous': x=(w-tw)/2: y=h/2: shadowx=1: shadowy=1: fontsize=h/15:fontcolor=orange)";

    bool test(std::string& s1, std::string& s2){
         if (s1 != s2){
            std::cout << "test FAILED result =" << s1 << " expected = " << s2 << std::endl;
            return false;
        }else{
            std::cout << s1 << std::endl;
            return true;
        }      
    }

    int main(int argc, char const *argv[])
    {
        auto content = json::parse(jsonData);//["meta"]["offering.en"];
        auto offer = content["meta"]["offering.en"];
        auto decoded_offer = base64_decode(offer);
        

        Dasher dasher('v', std::string("EN"), std::string("360p"), std::string("1"), decoded_offer, std::string(jsonWatermark));
        CommandlineGenerator clg(dasher.p, qlibid.c_str(), qhash.c_str());
        dasher.initialize();
        std::string strFilter = clg.GetFilterWatermark();
        std::cout << "full filter line = " << strFilter << std::endl;

        bool success = true;
        if(!test(strFilter, expectedResult))
            success = false;

        
        std::string watermark = "{{User.Address}}";
        std::string result = clg.ExpandWatermark(watermark);
        std::string calling_user = GetCallingUser();

        if (!test(result, calling_user))
            success = false;

        std::string watermark2 = "{{User.Address}}{{Node.Address}}";
        std::string result2 = clg.ExpandWatermark(watermark2);
        std::string expected2 = calling_user;
        expected2 += qhash;
        if (!test(result2, expected2))
            success = false;

        std::string watermark3 = "{{User.Address}}FooFey{{Node.Address}}Goober {{Library.Id}} ";
        std::string result3 = clg.ExpandWatermark(watermark3);
        std::string expected3 = calling_user;
        expected3 += std::string("FooFey");
        expected3 += qhash;
        expected3 += std::string("Goober "); 
        expected3 += qlibid;
        expected3 += " ";
        if (!test(result3, expected3))
            success = false;

        if (!success)           
            std::cout << "test for watermark expansion FAILED" << std::endl;
        else
            std::cout << "TEST PASSES" << std::endl;




        return 0;
    }
    
