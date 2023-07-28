#pragma once
/*
* Copyright 2023 Igalia S.L.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include <sys/time.h>
#include <time.h>
#include <errno.h>

using byte = uint8_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

constexpr int KiloByte = 1024;
#define ToKiloByte(X) ((X) / KiloByte)
constexpr int MegaByte = 1024 * 1024;
#define ToMegaByte(X) ((X) / float(MegaByte))

// To control the verbosity of the program.
extern int debuglevel;

// Optionally prints a timestamp, then prints the given info.  This should be used only at the beginning of a new line.
#define TSFPRINTF(stream, ...) (                  \
    fprintf(stream, "%s", util::TimestampStr()), \
    fprintf(stream, __VA_ARGS__), fflush(stream))

// if level <= debuglevel, print timestamp, then prints provided debug info
#define DEBUG(level, ...) (level <= debuglevel ? fprintf(stderr, "%s", util::timestamp_str()), \
    fprintf(stderr, __VA_ARGS__), fflush(stderr) : 0)

// same as DEBUG but does not print time stamp info
#define PDEBUG(level, ...) (level <= debuglevel ? fprintf(stderr, __VA_ARGS__), fflush(stderr) : 0)

// if errno != 0, report the errno and fprintf the ... varargs on stderr.
#define ERROR(errno, ...) ((errno == 0 ? (void)nullptr : perror("syscall failed")), \
    fprintf(stderr, "%s", util::TimestampStr()),                                   \
    fprintf(stderr, __VA_ARGS__),                                                   \
    fflush(stderr))
// same as ERROR, but also breaks into the debugger
#define XERROR(errno, ...) ((errno == 0 ? (void)nullptr : perror("syscall failed")), \
    fprintf(stderr, "%s", util::TimestampStr()),                                    \
    fprintf(stderr, __VA_ARGS__),                                                    \
    fflush(stderr),                                                                  \
    abort(), \
    exit(1));


#define ASSERT(expr)   assert(expr)

namespace util {

struct sized_buffer {
    u8* bytes;
    long len;
    u32 has_error;
    u32 padding;
};

void FreeSizedBuffer(sized_buffer* B)
{
    if (!B || !B->bytes)
        return;
    free(B->bytes);
    B->len = 0;
}

const char* TimestampStr(bool produce = true)
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

sized_buffer ReadWholeBinaryFileIntoMemory(const char* inFilename)
{
    sized_buffer res = {};
    res.bytes = nullptr;
    res.len = 0;
    res.has_error = true;

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

bool StrHasPrefix(const char *str, const char* prefix)
{
    while (*prefix) {
        if (*prefix != *str)
            return false;
        prefix++;
        str++;
    }
    return true;
}

bool StrToInt(const char* str, int base, int& out)
{
    char* endptr;
    errno = 0;
    out = static_cast<int>(strtol(str, &endptr, base));
    return !(errno != 0 || *endptr != '\0');
}

const char* StrRemovePrefix(const char* str, const char* prefix)
{
    while (*prefix) {
        if (*prefix != *str)
            return nullptr;
        prefix++;
        str++;
    }
    return str;
}

int StrFindIndex(const char* haystack, const char* needle)
{
    int needle_len = strlen(needle);
    int haystack_len = strlen(haystack);
    if (needle_len > haystack_len)
        return -1;
    for (int i = 0; i < haystack_len - needle_len; i++) {
        if (strncmp(haystack + i, needle, needle_len) == 0)
            return i;
    }
    return -1;
}

bool StrCaseInsensitiveSubstring(const char* haystack, const char* needle)
{
    int needle_len = strlen(needle);
    int haystack_len = strlen(haystack);
    if (needle_len > haystack_len)
        return -1;
    for (int i = 0; i < haystack_len - needle_len; i++) {
        if (strncasecmp(haystack + i, needle, needle_len) == 0)
            return true;
    }
    return false;
}

bool StrEqual(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

inline i32 RoundUp32(i32 a, i32 b)
{
    i32 d = a / b;
    return d * b == a ? a : (d + 1) * b;
}

inline u64 RDTSC()
{
    u32 lo, hi;
    __asm__ __volatile__("rdtsc"
                         : "=a"(lo), "=d"(hi));
    return static_cast<u64>(hi) << 32 | lo;
}

} // namespace util

