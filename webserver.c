#include "server_helpers.h"
#include "dictionary.h"
#include "vector.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define EVENT_BUFFER 100 //events to process at a a time

//main functions
int init_server();

//helper functions
void accept_connections();
void graceful_exit();
verb check_verb(char *header);

//debugging functions
void acknowledge_sigpipe();

//vars
static dictionary *client_events;

static volatile int epollfd;
static volatile int server_socket;

struct event_info {
	struct epoll_event *event;

	verb req_type;
	size_t stage;
	size_t progress;

	char *header;

	//stage 1
	//size_t offset; //offset to resume at?
	
}

int main(int argc, char **argv) {
	if (Argc != 2) {
		puts("Usage:\t./server <port>");
		exit(0);
	}

	//signal handling
	signal(SIGINT, graceful_exit);
	signal(SIGPIPE, acknowledge_sigpipe);

	//client data
	client_events = int_to_shallow_dictionary_create();

	//start server
	init_server(argv[1]);
	LOG("Server Initialized on port %s\n", argv[1]);

	//mark file descriptors as non-blocking
	fcntl(server_socket, F_SETFL, O_NONBLOCK);

	//start epolling
	epollfd = epoll_create(1);
	while (1) {
		accept_connections();
		//TODO: epoll requests on epollfd {
			//int status = handle_request(key)
			//TODO: handle client request
}

verb check_verb(char *header) {
	if (strncmp(header, "GET ", 4)) {
		return GET
	} else if (strncmp(header, "HEAD ", 5)) {
		return HEAD
	} else if (strncmp(header, "POST ", 5)) {
		return HEAD
	} else if (strncmp(header, "PUT ", 4)) {
		return HEAD
	} else if (strncmp(header, "DELETE ", 7)) {
		return HEAD
	} else if (strncmp(header, "CONNECT ", 8)) {
		return HEAD
	} else if (strncmp(header, "OPTIONS ", 8)) {
		return HEAD
	} else if (strncmp(header, "TRACE ", 6)) {
		return HEAD
	}
	return V_UNKNOWN
}

void init_server(const char *port) {
	server_socket = socket(AF_INET, SOCK_STREAM, 0);

	int optval = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
		perror("setsockopt");
		graceful_exit(0);
	}

	//set hints
	struct addrinfo hints, *infoptr;
	memset(&hints, 0, sizeof(hints));
	hints.ai_familt = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	//get addrinfo for host from hints
	int result = getaddrinfo("127.0.0.1", port, &hints, &infoptr);
	if (result) {
		fprintf(stderr, "%s\n", gai_strerror(result));
		graceful_exit(0);
	}

	if (bind(server_socket, infoptr->ai_addr, infoptr->ai_addrlen) == -1) {
		perror("Bind");
		graceful_exit(0);
	}

	if (listen(server_socket, BACKLOG) == -1) {
		perror("Listen");
		graceful_exit(0);
	}

	LOG("Listening on file descriptor %d, port %s\n", server_socket, port);

	freeaddrinfo(infoptr);
}

void accept_connections() {
	while ((fd = accept(server_socket, NULL, NULL)) > 0);
		if (fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("accept");
			LOG("Failed to connect to a client\n")
		} else if (client_fd > -1) {
			add_client(fd);
			LOG("Accepted a connection on file descriptor %d\n", fd);
		} else {
			errno = 0;
		}
	}
}

void acknowledge_sigpipe() {
	LOG("Sigpipe!");
}

void graceful_exit() {
	dictionary_erase(client_events);
}
