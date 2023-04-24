/* UTIL_HPP */
/* Copyright (C) 2023 Igalia. S.L. All rights reserved. */

#pragma once

#include <cstdio>
#include <cstdlib>
#include <sys/time.h>
#include <time.h>
#include <cstdint>

using byte = uint8_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

extern int debuglevel;

/* Optionally prints a timestamp, then prints the given info.  This should
   be used only at the beginning of a new line.  */
#define TSFPRINTF(stream, ...) (                  \
    fprintf(stream, "%s", util::timestamp_str()), \
    fprintf(stream, __VA_ARGS__), fflush(stream))

/* if level <= debuglevel, print timestamp, then prints provided debug info */
#define DEBUG(level, ...) (level <= debuglevel ? fprintf(stderr, "%s", util::timestamp_str()), \
    fprintf(stderr, __VA_ARGS__), fflush(stderr) : 0)

/* same as DEBUG but does not print time stamp info */
#define PDEBUG(level, ...) (level <= debuglevel ? fprintf(stderr, __VA_ARGS__), fflush(stderr) : 0)

/* if errno != 0,
   report the errno and fprintf the ... varargs on stderr. */
#define ERROR(errno, ...) ((errno == 0 ? (void)nullptr : perror("syscall failed")), \
    fprintf(stderr, "%s", util::timestamp_str()),                                   \
    fprintf(stderr, __VA_ARGS__),                                                   \
    fflush(stderr))
/* same as ERROR, but also exits with status 1 */
#define XERROR(errno, ...) ((errno == 0 ? (void)nullptr : perror("syscall failed")), \
    fprintf(stderr, "%s", util::timestamp_str()),                                    \
    fprintf(stderr, __VA_ARGS__),                                                    \
    fflush(stderr),                                                                  \
    exit(1))

namespace util {

struct sized_buffer {
    u8* bytes;
    long len;
    u32 has_error;
    u32 padding;
};

void FreeSizedBuffer(sized_buffer* B);
void FreeSizedBuffer(sized_buffer* B)
{
    if (!B || !B->bytes)
        return;
    free(B->bytes);
    B->len = 0;
}

const char* timestamp_str(bool produce = true);
const char* timestamp_str(bool produce)
{
    static char out[50];
    char* ptr;
    struct timeval dbgtv;
    struct tm* ts_tm;

    if (produce) {
        gettimeofday(&dbgtv, nullptr);
        ts_tm = localtime(&dbgtv.tv_sec);
        ptr = out + strftime(out, sizeof(out), "%H:%M:%S", ts_tm);
        snprintf(ptr, 13, ".%6.6ld ", dbgtv.tv_usec);
    } else {
        out[0] = 0;
    }
    return out;
}

sized_buffer ReadWholeBinaryFileIntoMemory(const char* inFilename);
sized_buffer ReadWholeBinaryFileIntoMemory(const char* inFilename)
{
    sized_buffer res {
        .bytes = nullptr,
        .len = 0,
        .has_error = true,
    };

    FILE* infile = fopen(inFilename, "rb");
    if (!infile)
        XERROR(errno, "Could not open file %s!\n", inFilename);

    // Get file size
    fseek(infile, 0, SEEK_END);
    res.len = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    // Allocate memory for the file contents
    res.bytes = static_cast<u8*>(malloc(static_cast<size_t>(res.len)));
    if (!res.bytes) {
        fprintf(stderr, "Memory allocation failed!\n");
        return res;
    }

    // Read the entire file into memory
    // FIXME: Doing it byte by byte to faciliate an easy time dealing with endianness later.
    byte* ptr = static_cast<byte*>(res.bytes);
    long size = res.len;
    while (size > 0) {
        if (fread(ptr++, 1, 1, infile) != 1)
            break;
    }

    if (!ferror(infile))
        res.has_error = false;

    // Free memory and return file
    fclose(infile);
    return res;
}

void ZeroMemory(void* _src, u32 len);
void ZeroMemory(void* _src, u32 len)
{
    u8* src = static_cast<u8*>(_src);
    for (u32 i = 0; i < len; i++)
        src[i] = 0;
}

inline i32 RoundUp32(i32 a, i32 b)
{
    i32 d = a / b;
    return d * b == a ? a : (d + 1) * b;
}

} // util


constexpr int KiloByte = 1024;
#define ToKiloByte(X) ((X) / KiloByte)
constexpr int MegaByte = 1024 * 1024;
#define ToMegaByte(X) ((X) / float(MegaByte))
