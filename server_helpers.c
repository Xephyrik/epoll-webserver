#include "server_helpers.h"

ssize_t read_header(int socket, char *buffer, size_t count) {

    if (count < 1) {
	return 0;
    }

    errno = 0;
    size_t progress = 0;
    while (progress < count && (progress < 2 || strncmp(buffer + progress-2, "\n\n", 2))) { //continue until a newline is in the buffer
	ssize_t result = read(socket, buffer + progress, 1);

	if (result > 0) {
	    progress += result;
	} else if (result == -1 && errno == EINTR) {
	    errno = 0;
	    continue;
	} else if (result == -1) {
	    return -1;
	} else {
	    return progress;
	}
    }
    LOG("%zu read\n", progress);
    return progress;
}

ssize_t write_all_to_socket(int socket, char *buffer, size_t count) {

    errno = 0;
    size_t progress = 0;
    while (progress < count) {
	ssize_t result = write(socket, buffer + progress, count - progress);
	if (result > 0) {
	    progress += result;
	} else if (result == -1 && errno == EINTR) {
	    errno = 0;
	    continue;
	} else if (result == -1) {
	    return -1;
	} else {
	    return progress;
	}
    }
    return progress;
}

ssize_t write_all_to_socket_from_file(int socket, FILE *file, size_t count, size_t offset) {

    void *buf[SOCKET_BUFFER < count ? SOCKET_BUFFER : count];

    fseek(file, offset, SEEK_SET);

    errno = 0;
    size_t progress = 0;
    while (progress < count && !feof(file)) {
	LOG("read %zu/%zu of file\n", progress, count); 
	size_t read_size = count - progress < SOCKET_BUFFER ? count - progress : SOCKET_BUFFER;

	size_t buf_progress = 0;
	fseek(file, offset + progress, SEEK_SET);

	size_t buf_size = fread(&buf, 1, read_size, file);
	assert(buf_size > 0);

	while (buf_progress < buf_size) {
	    LOG("buf: %zu/%zu\n", buf_progress, buf_size);
	    ssize_t result = write(socket, &buf + buf_progress, buf_size - buf_progress);

	    if (result > 0) {
		LOG("Wrote %zu\n", result);
		progress += result;
		buf_progress += result;
	    } else if ( result == -1 && errno == EINTR) {
		errno = 0;
		continue;
	    } else if (result == -1) {
		return -1;
	    } else {
		return progress;
	    }
	}
    }
    return progress;
}
ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {

    errno = 0;
    size_t progress = 0;
    while (progress < count) {
	ssize_t result = read(socket, buffer + progress, count - progress);

	if (result > 0) {
	    progress += result;
	} else if (result == -1 && errno == EINTR) {
	    errno = 0;
	    continue;
	} else if (result == -1) {
	    return -1;
	} else {
	    return progress;
	}
    }
    return progress;
}

ssize_t read_all_from_socket_to_file(int socket, FILE *file, size_t count, size_t offset) {

    void *buf[SOCKET_BUFFER < count ? SOCKET_BUFFER : count];

    fseek(file, offset, SEEK_SET);

    errno = 0;
    size_t progress = 0;
    while (progress < count) {
	size_t read_size = count - progress < SOCKET_BUFFER ? count - progress : SOCKET_BUFFER;
	ssize_t read_bytes = read(socket, &buf, read_size);

	if (read_bytes > 0) {
	    //write the buffer to the file
	    fseek(file, offset + progress, SEEK_SET);
	    fwrite(&buf, read_bytes, 1, file);

	    progress += read_bytes;
	} else if (read_bytes == -1 && errno == EINTR) {
	    errno = 0;
	    continue;
	} else if (read_bytes == -1) {
	    return -1;
	} else {
	    return progress;
	}
    }
    return progress;
}

ssize_t get_message_size(int socket) {
    int32_t size;
    ssize_t read_bytes = read_all_from_socket(socket, (void *)&size, MESSAGE_SIZE_DIGITS);
    if (read_bytes == 0 || read_bytes == -1)
        return read_bytes;

    return (ssize_t)size;
}

ssize_t write_message_size(int socket, size_t size) {
    return write_all_to_socket(socket, (void *)&size, MESSAGE_SIZE_DIGITS);
}
