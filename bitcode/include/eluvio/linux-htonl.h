#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include <stdint.h>
class host_network_byte_order{
public:
  static uint32_t ntohl32(uint32_t networklong) {
    uint8_t input[4];
    memcpy(input, &networklong, 4);
    return ((((uint32_t) input[0]) << 24) |
            (((uint32_t) input[1]) << 16) |
            (((uint32_t) input[2]) << 8) |
            ((uint32_t) input[3]));
  }
  static uint32_t htonl32(uint32_t hostlong) {
    uint8_t result_bytes[4];
    result_bytes[0] = (uint8_t) ((hostlong >> 24) & 0xFF);
    result_bytes[1] = (uint8_t) ((hostlong >> 16) & 0xFF);
    result_bytes[2] = (uint8_t) ((hostlong >> 8) & 0xFF);
    result_bytes[3] = (uint8_t) (hostlong & 0xFF);
    uint32_t result;
    memcpy(&result, result_bytes, 4);
    return result;
  }
  static uint16_t htons16(uint16_t hostshort) {
    uint8_t result_bytes[2];
    result_bytes[0] = (uint8_t) ((hostshort >> 8) & 0xFF);
    result_bytes[1] = (uint8_t) (hostshort & 0xFF);
    uint16_t result;
    memcpy(&result, result_bytes, 2);
    return result;
  }
  static uint16_t ntohs16(uint16_t networkshort) {
    uint8_t input[2];
    memcpy(input, &networkshort, 2);

    return ((((uint32_t) input[0]) << 8) |
            ((uint32_t) input[1]));
  }

  static uint64_t htnol64(uint64_t hostll){
     return (((uint64_t)host_network_byte_order::htonl32(hostll)) << 32) + host_network_byte_order::htonl32((hostll) >> 32);
  }

};
