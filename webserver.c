#include "server_helpers.h"
#include "dictionary.h"
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

#define BACKLOG 10
#define TIMEOUT_MS 1000
#define EVENT_BUFFER 100


void init_server();
int handle_request(int *key);
void accept_connections();
void add_client(int fd);
void remove_client(int *key);
void graceful_exit(int);
verb check_verb(char *header);

//vars
static dictionary *client_requests;

static volatile int epollfd;
static volatile int server_socket;

struct request_info {
	struct epoll_event *event;

	verb req_type;
	size_t stage;
	size_t progress;

	char *buffer; //header
};

//initialize server and poll for requests
int main(int argc, char **argv) {
	if (argc != 2) {
		puts("Usage:\t./server <port>");
		exit(0);
	}

	//signal handling
	signal(SIGINT, graceful_exit);

	//client data
	client_requests = int_to_shallow_dictionary_create();

	//start server
	init_server(argv[1]);
	LOG("Server Initialized on port %s\n", argv[1]);

	//mark file descriptors as non-blocking
	fcntl(server_socket, F_SETFL, O_NONBLOCK);

	//start epolling
	epollfd = epoll_create(1);
	while (1) {
		accept_connections();
		
		struct epoll_event array[EVENT_BUFFER];

		//Get events
		int num_events = epoll_wait(epollfd, array, EVENT_BUFFER, TIMEOUT_MS);
		if (num_events == -1) {
			perror("epoll_wait");
			graceful_exit(0);
		}

		//Handle events
		for (int i = 0; i < num_events; i++) {
			void *key = &array[i];
			int event = array[i].events;

			if (event & EPOLLIN) {

				int status = handle_request(key); //process request
				if (status > 0) { //remove client on success, sigpipe, or error
					remove_client(key);
				}
			}
			if (event & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
				remove_client(key);
			}
		}
	}
}

//add client to epoll and the request dictionary
void add_client(int fd) {
	struct epoll_event *ev = calloc(1, sizeof(struct epoll_event));
	ev->events = EPOLLIN | EPOLLET;
	ev->data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, ev);

	if (!dictionary_contains(client_requests, &ev->data.fd)) {
		struct request_info *req_info = calloc(1, sizeof(struct request_info));
		req_info->event = ev;
		dictionary_set(client_requests, &ev->data.fd, req_info);
		LOG("Added client %d\n", fd);

	} else {
		LOG("Tried to add alread-existing key for fd %d to client_requests\n", fd);
	}
}

//remove client from epoll and the request dictionary
void remove_client(int *key) {

	if (dictionary_contains(client_requests, key)) {
		int fd = *key;
		epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);

		struct request_info *req_info = dictionary_get(client_requests, key);
		free(req_info->event);
		if (req_info->buffer) {
			free(req_info->buffer);
		}
		free(req_info);

		dictionary_remove(client_requests, key);
		shutdown(fd, SHUT_RDWR);
		close(fd);

		LOG("Removed client %d\n", fd);
	} else { //for debugging
		LOG("Tried to remove non-existent key for fd %d from client_requests\n", *key);
	}
}

//stage 0: read in header
//stage 1+: process command
//returns 0 on block, 1 on success, 2 on sigpipe/error
int handle_request(int *key) {
	int fd = *key;
	struct request_info *req_info = dictionary_get(client_requests, key);

	//Stage 0: Read Header
	if (req_info->stage == 0) {
		if (req_info->buffer == NULL) {
			req_info->buffer = malloc(MAX_HEADER_SIZE*sizeof(char));
		}

		ssize_t read_status = read_header(fd, req_info->buffer + req_info->progress, MAX_HEADER_SIZE);
		//Did we make progress?
		if (read_status > 0) {
			req_info->progress += read_status;
		}

		//Return on block/error, otherwise go to next stage
		if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
			return 2;
		} else if (errno != 0) {
			return 3;
		} else {
			req_info->stage = 1;
			req_info->progress = 0;
		}
	}		
	//Stage 1+: Process Request


	return 1; //successfully reached the end
}

//return the type of request indicated by the beginning of the header
verb check_verb(char *header) {
	if (strncmp(header, "GET ", 4)) {
		return GET;
	} else if (strncmp(header, "HEAD ", 5)) {
		return HEAD;
	} else if (strncmp(header, "POST ", 5)) {
		return POST;
	} else if (strncmp(header, "PUT ", 4)) {
		return HEAD;
	} else if (strncmp(header, "DELETE ", 7)) {
		return HEAD;
	} else if (strncmp(header, "CONNECT ", 8)) {
		return HEAD;
	} else if (strncmp(header, "OPTIONS ", 8)) {
		return HEAD;
	} else if (strncmp(header, "TRACE ", 6)) {
		return HEAD;
	}
	return V_UNKNOWN;
}

//initialize server
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
	hints.ai_family = AF_INET;
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

//accept pending connections
void accept_connections() {
	int fd;
	while ((fd = accept(server_socket, NULL, NULL)) > 0) {
		if (fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("accept");
			LOG("Failed to connect to a client\n")
		} else if (fd > -1) {
			add_client(fd);
			LOG("Accepted a connection on file descriptor %d\n", fd);
		} else {
			errno = 0;
		}
	}
}

void graceful_exit(int arg) {
	dictionary_destroy(client_requests);
	//more?
	exit(0);
}
