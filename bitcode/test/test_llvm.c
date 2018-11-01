/*
  Build commands:

  /usr/local/opt/llvm/bin/clang -O0 -ggdb -I .. -Wall -emit-llvm -c avmasterimf.c fixup.c ../qspeclib.c
  /usr/local/opt/llvm/bin/llvm-link -v avmasterimf.bc fixup.bc qspeclib.bc -o avmaster.imf.bc
*/

#ifndef __WASMCEPTION_H
#define __WASMCEPTION_H

#define WASM_EXPORT __attribute__ ((visibility ("default")))

#endif // __WASMCEPTION_H

#include "../qspeclib.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>

/* Core and Extended API */
extern int64_t FFMPEGRun(char*, int, char*, int);
extern void LOGMsg(char *);
extern int KVSet(char *, char *, char *, char *);
extern char *KVGetTemp(char *, char *, char *);
extern int KVPushBack(char *, char *, char *, char *);
extern char *KVRangeTemp(char *, char *, char *);
extern char *QCreatePart(char *, char *, char *, uint32_t);
extern char *QReadPart(char *, char *, char *, uint32_t *);
extern char *KVGet(char *, char *, char *);
extern char *KVRange(char *, char *, char *);

/*
 * Echos the passed-in arguments as out args.
 *
 * Arguments:
 *   any arguments
 * Return:
 *   the arguments passed in
 */
int
echo(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)


    char *outargv[argc];
    uint32_t outargsz[argc];

    for (int i=0; i< argc; i++) {
        printf("echo index [%d] arg [%s]\n", i + 1, argv[i]);
        outargv[i] = argv[i];
        outargsz[i] = argsz[i];
        usleep(200000);
    }

    argv2buf(argc, (const char **)outargv, outargsz, outbuf, outsz);
    freeargv(argc, argv, argsz);
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
int
factorial(int outsz, char *outbuf, const char *argbuf)
{
    // read function args
    int argc = 0;
    char **argv = NULL;
    uint32_t *argsz = NULL;
    argc = buf2argv(argbuf, &argv, &argsz);  // don't forget freeargv(argv)

    if (argc != 1) {
        return -1;
    }

    int n = atoi(argv[0]);
    char *outargv[1];
    uint32_t outargsz[1];

    printf("calculating factorial of [%s]/int[%d]\n", argv[0], n);

    int fact = 1;
    for (int i=1; i<=n; i++) {
        fact = fact * i;
    }

    char buf[20];
    snprintf(buf, 20, "%d", fact);

    outargv[0] = buf;
    outargsz[0] = strlen(buf);

    argv2buf(argc, (const char **)outargv, outargsz, outbuf, outsz);
    freeargv(argc, argv, argsz);
    return 0;
}
