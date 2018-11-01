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
#include <vector>
#include <memory>

#include "linux-htonl.h"

class ArgumentBuffer{
public:
        ArgumentBuffer(const char* argbuf) : m_argbuf(argbuf){
        m_argc = buf2argv();
    }
    ~ArgumentBuffer(){
        int i;
        if (m_argv){
            for (i = 0; i < m_argc; i ++) {
                if (m_argv[i]) free(m_argv[i]);
            }
            free(m_argsz);
            free(m_argv);
        }
    }

      static int argv2buf(std::vector<std::string>& inputs,  char *argbuf, int argbufsz){
        int argc = inputs.size();
        char** argv = (char**)alloca(argc*sizeof(char*));
        uint32_t* argsz = (uint32_t*)alloca(argc*sizeof(uint32_t*));
        for (int i = 0; i < argc; i++){
            argv[i] = (char*)inputs[i].c_str();
            argsz[i] = inputs[i].length();
        }
        return argv2buf(argc,(const char**)argv,argsz, argbuf,argbufsz);

    }    

    /*
    * Convert a standard char *argv[] with specified sizes to an
    * encoded arg buffer.  Sizes should not include null-terminators (the
    * encoded arguments are not expected to have null-terminators)
    */
    static int argv2buf(uint16_t argc, const char*argv[], uint32_t argsz[], char *argbuf, int argbufsz){
        uint32_t i, pos = 0;
        uint32_t nargs = host_network_byte_order::htonl32(argc);
        memcpy(argbuf, (char *)&nargs, sizeof(nargs));
        pos += sizeof(nargs);
        for (i = 0; i < argc; i ++) {
            uint32_t sz, nsz;
            sz = argsz[i];
            nsz = host_network_byte_order::htonl32(sz);
            memcpy(argbuf + pos, (char *)&nsz, sizeof(nsz));
            memcpy(argbuf + pos + sizeof(sz), argv[i], sz);
            pos += sizeof(nsz) + sz;
        }
        return pos;
    }

    operator char**(){
        return m_argv;
    }
    char* operator[](int i){
        return (m_argv)[i];
    }
    int sizeAt(int i){
        return m_argsz[i];
    }
    int Count(){ return m_argc;}

private:
    int m_argc;
    const char* m_argbuf;
    char** m_argv;
    uint32_t *m_argsz;

    /*
    * Convert the marshalled argument buffer to a standard 'char *argv[]' with
    * NULL-terminated elements.  The encoded arguments are not null-terminated
    * but since they might need to be interpreted as strings, the null-terminator
    * is added anyway.  The function returns the sizes of the arguments as
    * well so the caller can interpret each argumen as either a string or an
    * arbitrary byte buffer as desired.
    *
    * Encoding convention:
    *
    *   [ num args  |   argsz   |  argdata  |   argsz   | argdata | ... ]
    *     4 bytes      4 bytes       var       4 bytes      var     ...
    *   Note: all integers are in network order (which happens to be big endian)
    *
    * Return:
    *   number of arguments or -1 for error
    */
    uint16_t buf2argv(){
        uint32_t nargs, i = 0, pos = 0;

        (void)memcpy((char *)&nargs, m_argbuf, sizeof(nargs));
        nargs = host_network_byte_order::ntohl32(nargs);
        m_argv = (char **)calloc(nargs, sizeof (char *));
        m_argsz = (uint32_t *)calloc(nargs, sizeof(uint32_t));
        pos += sizeof(nargs);

        for (i = 0; i < nargs; i ++) {
            uint32_t sz;
            memcpy((char *)&sz, m_argbuf + pos, sizeof(sz));
            sz = host_network_byte_order::ntohl32(sz);
            m_argsz[i] = sz;
            if (sz > 0) {
                m_argv[i] = (char *)malloc((sz + 1) * sizeof(char));
                memcpy(m_argv[i], m_argbuf + pos + sizeof(sz), sz);
                m_argv[i][sz] = '\0';
            }
            pos += sizeof(sz) + sz;
        }

        return nargs;
    }
    
};
