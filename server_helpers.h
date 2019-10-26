#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#ifdef DEBUG
    #define LOG(args...) fprintf(stderr, args)
#else
    #define LOG(...)
#endif

#define MESSAGE_SIZE_DIGITS sizeof(size_t)
#define MAX_HEADER_SIZE 1024 //413 Entity Too large if header size > MAX_HEADER_SIZE
#define MAX_FILENAME_SIZE 255
#define SOCKET_BUFFER 8096

typedef enum { GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, V_UNKNOWN } verb;

ssize_t read_header(int socket, char *buffer, size_t max_length);

ssize_t write_all_to_socket(int, char *, size_t);

ssize_t write_all_to_socket_from_file(int, FILE *, size_t, size_t);

ssize_t read_all_from_socket(int, char *, size_t);

ssize_t read_all_from_socket_to_file(int, FILE *, size_t, size_t);
