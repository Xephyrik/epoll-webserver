#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#ifdef DEBUG
    #define LOG(args...) fprintf(stderr, args)
#else
    #define LOG(...)
#endif

#define MAX_HEADER_SIZE 8092 //413 Entity Too large if header size > MAX_HEADER_SIZE
#define MAX_PATHNAME_SIZE 4096
#define SOCKET_BUFFER 8092
#define MAX_REQUEST_HEADER_FIELD_SIZE 256

typedef enum { GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, V_UNKNOWN } verb;

ssize_t read_header(int socket, char *buffer, size_t max_length);

ssize_t write_all_to_socket(int, char *, size_t);

ssize_t write_all_to_socket_from_file(int, FILE *, size_t, size_t);

ssize_t read_all_from_socket(int, char *, size_t);

ssize_t read_all_from_socket_to_file(int, FILE *, size_t, size_t);
