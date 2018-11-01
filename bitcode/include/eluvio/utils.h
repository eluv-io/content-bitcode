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
#include <memory>
#include <assert.h>
#include "eluvio/argutils.h"
#include "eluvio/el_cgo_interface.h"

using namespace std;

std::unique_ptr<char, decltype(free)*> CHAR_BASED_AUTO_RELEASE(char* x){
    return std::unique_ptr<char, decltype(free)*>{ x, free };
}

static const std::string& getBase64Chars(){
    static const std::string base64_chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789+/";
    return base64_chars;
}


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += getBase64Chars()[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++)
      ret += getBase64Chars()[char_array_4[j]];

    while((i++ < 3))
      ret += '=';

  }

  return ret;

}

std::string base64_decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = getBase64Chars().find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j <4; j++)
      char_array_4[j] = 0;

    for (j = 0; j <4; j++)
      char_array_4[j] = getBase64Chars().find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }

  return ret;
}

class MutexWait{
public:
    MutexWait(bool bVideo) :mutex(bVideo ? MutexWait::GetVideoMutex() : MutexWait::GetAudioMutex()){ 
        locked = false;
    }
    ~MutexWait(){
        if (locked)
            if (pthread_mutex_unlock( &mutex ) < 0)
                printf("Error mutex unlock failed\n");
    }

    void Wait(){
        printf("Entering Wait..\n");
        if (pthread_mutex_lock( &mutex ) < 0)
            printf("Error mutex lock failed\n");
        locked = true;
        printf("Leaving Wait..\n");
    }

private:
    static pthread_mutex_t& GetVideoMutex(){
        static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        return mutex;

    }
    static pthread_mutex_t& GetAudioMutex(){
        static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        return mutex;

    }
    pthread_mutex_t& mutex;
    bool locked;
};

class MemoryUtils{
public:

    static int packStringArray( char* inputBuffer, const char* const * arrayToPack, int stringCount){
        uint32_t* begin = (uint32_t *)inputBuffer;
        inputBuffer += sizeof(int);  //skip over size of struct
        int bufLen = sizeof(int);
        for (int i = 0; i < stringCount; i++){
            const char* curString = arrayToPack[i];
            uint32_t len = strlen(curString);
            *((uint32_t*)(inputBuffer)) = host_network_byte_order::htonl32(len);
            inputBuffer += sizeof(int);
            strcpy(inputBuffer, curString);
            inputBuffer += len;
            bufLen += len + sizeof(int);
        }
        *((uint32_t*)(begin)) = host_network_byte_order::htonl32(stringCount);
        return bufLen;
    }

};



