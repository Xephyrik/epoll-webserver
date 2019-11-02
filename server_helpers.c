#include "server_helpers.h"

ssize_t read_header(int socket, char *buffer, size_t count) {

    if (count < 1) {
        return 0;
    }

    errno = 0;
    size_t progress = 0;
    while (progress < count) { //continue until a newline is in the buffer
        ssize_t result = read(socket, buffer + progress, 1);

        if (result > 0) { 
        progress += result;

        //if we detect end of header, return
        if (progress >= 2 && strncmp(buffer + progress - 2, "\n\n", 2) == 0) {
            LOG("\t\t%zu read. Done (\\n\\n)\n", progress);
            return progress;
        } else if (progress >= 4 && strncmp(buffer + progress - 4, "\r\n\r\n", 4) == 0) {
            LOG("\t\t%zu read. Done (\\r\\n\\r\\n)\n", progress);
            return progress;
        }

        } else if (result == -1 && errno == EINTR) {
            errno = 0;
            continue;
        } else if (result == -1) {
            perror("Header Read Error");
            return -1;
        } else { //result == 0, we are finished
            LOG("\t\t%zu read. Done (0)\n", progress);
            return progress;
        }
    }
    LOG("\t\t%zu read. Done (too long)\n", progress);
    return -1;
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
            perror("Write Error");
            return -1;
        } else {
            return progress;
        }
    }
    return progress;
}

ssize_t write_all_to_socket_from_file(int socket, FILE *file, size_t count, size_t offset) {

    LOG("\n\t\tStarting read at %zu/%zu of file\n", offset, count + offset); 
    void *buf[SOCKET_BUFFER < count ? SOCKET_BUFFER : count];

    fseek(file, offset, SEEK_SET);

    int writes = 0;

    errno = 0;
    size_t progress = 0;
    while (progress < count && !feof(file)) {

        size_t read_size = count - progress < SOCKET_BUFFER ? count - progress : SOCKET_BUFFER;
        
        size_t buf_progress = 0;
        fseek(file, offset + progress, SEEK_SET);
 
        size_t buf_size = fread(&buf, 1, read_size, file);
        assert(buf_size > 0);
 
        while (buf_progress < buf_size) {
            ssize_t result = write(socket, &buf + buf_progress, buf_size - buf_progress);
 
            if (result > 0) {
                progress += result;
                buf_progress += result;
		writes += 1;
	    } else if ( result == -1 && errno == EINTR) {
                errno = 0;
                continue;
            } else if (result == -1) {
                perror("\t\tWrite Error");
                return -1;
            } else {
                return progress;
            }
        }
    }
    LOG("\t\tWrite Result: %zu; %d writes\n\n", progress, writes);
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
            perror("Read Error");
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
            perror("Read Error");
            return -1;
        } else {
            return progress;
        }
    }
    return progress;
}
