#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator>
#include <string.h>
#include <string>
#include <limits.h>
#include <unistd.h>
#include <algorithm>
#include <ctime>
#include <chrono>
#include "eluvio/argutils.h"
#include "eluvio/utils.h"

/*
 * Echos the passed-in arguments as out args.
 *
 * Arguments:
 *   any arguments
 * Return:
 *   the arguments passed in
 */
extern "C" int
echo(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    ArgumentBuffer argBuf(argbuf); 
    argc = argBuf.Count();


    char *outargv[argc];
    uint32_t outargsz[argc];

    std::string msg = "echo";
    for (int i=0; i< argc; i++) {
        msg += ", ";
        msg += argBuf[i];
        outargv[i] = argBuf[i];
        outargsz[i] = argBuf.sizeAt(i);
    }

//    auto start = std::chrono::system_clock::now();

    int timeout = 600000;
    if (argc >= 3) {
        timeout = atoi(argBuf[2]);
//        printf("Sleeping for %d micros, arg[1]=%s arg[2]=%s\n", timeout, argBuf[1], argBuf[2]);
    } else {
        printf("Sleeping for %d micros, num args=%d\n", timeout, argc);
    }
    usleep(timeout);

//    auto end = std::chrono::system_clock::now();

//    std::time_t end_c = std::chrono::system_clock::to_time_t(end);
//    std::stringstream buf;
//    buf << "#### " << std::put_time(std::localtime(&end_c), "%F %T") << " " << msg << " duration=" << (end-start).count() << "\n";
//    printf("%s", buf.str().c_str());

    ArgumentBuffer::argv2buf(argc, (const char **)outargv, outargsz, outbuf, outsz);
    return 0;
}

/*
 * Calculates the factorial n! of the input argument
 *
 * Arguments:
 *   n string            the base 10 string of n
 * Return:
 *   factorial string    the base 10 string of the factorial
 */
extern "C" int
factorial(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    ArgumentBuffer argBuf(argbuf); 
    argc = argBuf.Count();

    if (argc != 1) {
        return -1;
    }

    int n = atoi(argBuf[0]);
    char *outargv[1];
    uint32_t outargsz[1];

    // printf("calculating factorial of [%s]/int[%d]\n", argBuf[0], n);

    int fact = 1;
    for (int i=1; i<=n; i++) {
        fact = fact * i;
    }

    char buf[20];
    snprintf(buf, 20, "%d", fact);

    outargv[0] = buf;
    outargsz[0] = strlen(buf);

    ArgumentBuffer::argv2buf(argc, (const char **)outargv, outargsz, outbuf, outsz);
    return 0;
}
