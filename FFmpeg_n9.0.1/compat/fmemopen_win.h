#ifndef FMEMOPEN_WIN_H
#define FMEMOPEN_WIN_H

#include <stdio.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

/**
 * Windows implementation of fmemopen.
 * Uses a temporary file in the system temp directory that is
 * automatically deleted when the file handle is closed.
 */
static FILE* fmemopen(void *buf, size_t size, const char *mode) {
    char temp_path[MAX_PATH];
    char temp_file[MAX_PATH];

    // Get the actual system temp directory (e.g., C:\Users\xxx\AppData\Local\Temp)
    // instead of "." (which is the bin directory)
    if (GetTempPathA(MAX_PATH, temp_path) == 0) return NULL;
    if (GetTempFileNameA(temp_path, "av3a", 0, temp_file) == 0) return NULL;

    // "wb+D" -> write/read binary, Delete on close (critical for cleanup)
    FILE* f = fopen(temp_file, "wb+D");
    if (!f) return NULL;

    if (size > 0 && buf) {
        fwrite(buf, 1, size, f);
        rewind(f);
    }
    return f;
}

#endif
