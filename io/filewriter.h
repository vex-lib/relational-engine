#ifndef IO_FILEWRITER_H
#define IO_FILEWRITER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// io/filewriter.h — the FileWriter class, ported from io/FileWriter.java.
//
// Buffered binary file writer used by Log on the writer thread. A 64 KB
// stdio buffer coalesces records into large writes.

#define FILEWRITER_BUFFER_SIZE (1 << 16)

typedef struct FileWriter {
    FILE *out;
    bool open;
    uint64_t bytes_written;
    char buffer[FILEWRITER_BUFFER_SIZE];
} FileWriter;

// Create missing parent dirs (if any), then create/truncate the file at path.
// Returns true on success.
bool FileWriter_open(FileWriter *w, const char *path);

// Write len Bytes from data into the buffered writer. No-op when closed.
void FileWriter_write(FileWriter *w, const uint8_t *data, size_t len);

void FileWriter_flush(FileWriter *w);
void FileWriter_close(FileWriter *w);

uint64_t FileWriter_bytesWritten(const FileWriter *w);

#endif