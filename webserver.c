#include "server_helpers.h"
//#include "dictionary.h"
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
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define BACKLOG 10
#define TIMEOUT_MS 1000
#define EVENT_BUFFER 100


typedef struct request_info request_info;

void init_server();
int handle_request(int fd);
void accept_connections();
int send_status(int fd, int status, struct request_info *);
int send_status_n(int fd, int status, struct request_info *, size_t content_length);
void add_client(int fd);
void remove_client(int fd);
void acknowledge_sigpipe(int);
void graceful_exit(int);
verb check_verb(char *header);

struct request_info *client_requests[100];

char *status_desc[510];

static char *ROOT_SITE = "Root";

static volatile int epollfd;
static volatile int server_socket;

struct request_info {
	struct epoll_event *event;

	verb req_type;
	size_t stage;
	size_t progress;

	char *request_h; //header
	char *response_h;
};

//initialize server and poll for requests
int main(int argc, char **argv) {
	if (argc != 2) {
		puts("Usage:\t./server <port>");
		exit(0);
	}

	//statu codes
	status_desc[200] = "OK";
	status_desc[204] = "No Content";
	status_desc[400] = "Bad Request";
	status_desc[401] = "Unauthorized";
	status_desc[404] = "Not Found";
	status_desc[405] = "Method Not Allowed";

	//signal handling
	signal(SIGINT, graceful_exit);
	signal(SIGPIPE, acknowledge_sigpipe);
	//start server
	init_server(argv[1]);
	LOG("Server Initialized on port %s\n", argv[1]);

	//mark file descriptors as non-blocking
	fcntl(server_socket, F_SETFL, O_NONBLOCK);

	//start epolling
	epollfd = epoll_create(1);
	LOG("Polling for requests");
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
			int fd = array[i].data.fd;
			int event = array[i].events;

			if (event & EPOLLIN) {

				LOG("Working on request for %d\n", fd);

				int status = handle_request(fd); //process request
				LOG("Status for %d: %d\n", fd, status);

				if (status > 0) { //remove client on success, sigpipe, or error
					remove_client(fd);
				}
			}
			if (event & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
				remove_client(fd);
			}
		}
	}
}

//add client to epoll and the requests array
void add_client(int fd) {
	struct epoll_event *ev = calloc(1, sizeof(struct epoll_event));
	ev->events = EPOLLIN | EPOLLET;
	ev->data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, ev);

	if (client_requests[fd] == NULL) {
		struct request_info *req_info = calloc(1, sizeof(struct request_info));
		req_info->event = ev;

		client_requests[fd] = req_info;		
		LOG("Added client %d\n", fd);

	} else {
		struct request_info *req_info = calloc(1, sizeof(struct request_info));
		req_info->event = ev;
		
		client_requests[fd] = req_info;		
		LOG("Added client (on top of another?!) %d\n", fd);
	}
}

//remove client from epoll and the requests array
void remove_client(int fd) {

	if (client_requests[fd]) {
		epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);

		struct request_info *req_info = client_requests[fd];
		free(req_info->event);
		if (req_info->request_h) {
			free(req_info->request_h);
		}
		if (req_info->response_h) {
			free(req_info->response_h);
		}

		free(req_info);

		client_requests[fd] = NULL;

		shutdown(fd, SHUT_RDWR);
		close(fd);

		LOG("Removed client %d\n", fd);
	} else { //for debugging
		LOG("Tried to remove non-existent key for fd %d from client_requests\n", fd);
	}
}

//stage 0: read in header
//stage 1+: process command
//returns 0 on block, 1 on success, 2 on sigpipe/error
int handle_request(int fd) {
	struct request_info *req_info = client_requests[fd];
	errno = 0; //just in case for now

	//Stage 0: Read Header
	if (req_info->stage == 0) {
		LOG("\tStage 0, %zu prior progress\n", req_info->progress);

		if (req_info->request_h == NULL) {
			LOG("\tHeader buffer allocated\n");
			req_info->request_h = calloc(1, MAX_HEADER_SIZE*sizeof(char));
		}

		ssize_t read_status = read_header(fd, req_info->request_h + req_info->progress, MAX_HEADER_SIZE - req_info->progress);
		LOG("\tRead status: %zu\n", read_status);

		//Did we make progress?
		if (read_status > 0) {
			req_info->progress += read_status;
		}

		LOG("Header: %s\n", req_info->request_h);

		//Return on block/error, otherwise go to next stage
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			LOG("Read blocked!\n");
			//Resume request later
			return 0;
		} else if (errno == SIGPIPE) {
			LOG("Sigpipe on %d\n", fd);
			//Ignore request
			return 3;
		} else if (errno != 0) { //SIGPIPE or error
			LOG("Error reading header\n");
			//Ignore request
			return 3;
		} else {
			LOG("completed reading header!\n");
			req_info->stage = 1;
			req_info->progress = 0;
		}
	}

	req_info->req_type = check_verb(req_info->request_h);
	//Stage 1+: Process Request
	if (req_info->req_type == V_UNKNOWN) {
		if (req_info->stage == 1) {
			if (req_info->response_h == NULL) {

				//Allocate space for the response header
				if (req_info->response_h == NULL) {
					req_info->response_h = calloc(1, MAX_HEADER_SIZE);
				}

				int ret = send_status(fd, 400, req_info);
				if (ret != 0) { //block or error
					return ret;
				}
			}
		}

	} else if (req_info->req_type == GET) {
		char filename[MAX_FILENAME_SIZE];

		//Check if we can scan the filename
		if (sscanf(req_info->request_h, "%*s %s", filename + strlen(ROOT_SITE)) != 1) {
			return send_status(fd, 400, req_info);
		}

		// filename = ROOT_SITE .. filename (init.html if needed)
		memcpy(filename, ROOT_SITE, strlen(ROOT_SITE));
		LOG("\tGET %s\n", filename);

		//Attach \"init.html\" if they specified a directory
		//as opposed to a file (i.e. favicon.ico)
		if (strchr(filename, '.') == NULL) {
			if (filename[strlen(filename)-1] == '/') {
				strncat(filename, "init.html", 11);
			} else {
				strncat(filename, "/init.html", 11);
			}
		}

		//Allocate space for the response header
		if (req_info->response_h == NULL) {
			req_info->response_h = calloc(1, MAX_HEADER_SIZE);
		}

		//Check if resource exists
		if (access(filename, F_OK) != 0) {
			return send_status(fd, 404, req_info);
		}

		//Check file size
		struct stat file_stat;
		if (stat(filename, &file_stat) == -1) {
			perror("stat");
			return 3;
		}
		size_t file_size = (size_t)file_stat.st_size;

		LOG("Final file path: %s, File size: %zu\n", filename, file_size);

		if (req_info->stage == 1) {

			//send response header, returning on block or error
			int ret = 0;
			if ((ret = send_status_n(fd, 200, req_info, file_size)) != 1) { 
				return ret;
			}
		}

		//Get the actual file path, file size, and then write as much as possible
		if (req_info->stage == 2) {

			FILE *file = fopen(filename, "r");

			ssize_t write_status = write_all_to_socket_from_file(fd, file, 
				file_size - req_info->progress, req_info->progress);

			fclose(file);

			//Did we make progress?
			if (write_status > 0) {
				req_info->progress += write_status;
			}

			LOG("Write progress: %zu\n", req_info->progress);

			//Return on block/error, otherwise go to next stage
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				LOG("Write blocked!\n");
				//Resume request later
				return 0;
			} else if (errno == SIGPIPE) {
				LOG("Sigpipe on %d\n", fd);
				//Ignore request
				return 3;
			} else if (errno != 0) { //SIGPIPE or error
				LOG("Error writing file\n");
				//Ignore request
				return 3;
			} else {
				LOG("Completed writing file!\n");
				return 1; //Success!
			}
	
		}

	} else {
		//Method not allowed (Not recognised)
		if (req_info->stage == 1) {
			if (req_info->response_h == NULL) {

				//Allocate space for the response header
				if (req_info->response_h == NULL) {
					req_info->response_h = calloc(1, MAX_HEADER_SIZE);
				}

				return send_status(fd, 405, req_info);
			}
		}
	}

	return 1; //successfully reached the end
}

int send_status(int fd, int status, struct request_info *req_info) {
	LOG("Preparing a status of %d\n", status);

	//Get formatted date
	char date[100];
	time_t now = time(0);
	struct tm tm = *gmtime(&now);
	strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %Z", &tm);

	//Format response
	sprintf(req_info->response_h, "HTTP/1.1 %d %s\n"
			"Date: %s\n"
			"Connection: close\n\n",
			status, status_desc[status], date);


	ssize_t write_status = write_all_to_socket(fd, 
			req_info->response_h + req_info->progress, 
			strlen(req_info->response_h) - req_info->progress);

	LOG("\n\tWrite status: %zu\nResponse:\n\"%s\"\n", write_status, req_info->response_h);

	//Did we make progress?
	if (write_status > 0) {
		req_info->progress += write_status;
	}

	//Return on block/error, otherwise go to next stage
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		LOG("Write blocked!\n");
		//Resume request later
		return 0;
	} else if (errno == SIGPIPE) {
		LOG("Sigpipe on %d\n", fd);
		//Ignore request
		return 3;
	} else if (errno != 0) { //SIGPIPE or error
		LOG("Error writing header\n");
		//Ignore request
		return 3;
	} else {
		LOG("Completed writing response!\n");
		req_info->stage += 1;
		req_info->progress = 0;
	}

	return 1;
}			

int send_status_n(int fd, int status, struct request_info *req_info, size_t file_size) {
	LOG("Sending a status of %d\n", status);

	char date[100];
	time_t now = time(0);
	struct tm tm = *gmtime(&now);
	strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %Z", &tm);

	sprintf(req_info->response_h, "HTTP/1.1 %d %s\n"
			"Date: %s\n"
			"Connection: close\n"
			"Content-Length: %zu\n\n",
			status, status_desc[status], date, file_size);

	ssize_t write_status = write_all_to_socket(fd, 
			req_info->response_h + req_info->progress, 
			strlen(req_info->response_h) - req_info->progress);

	LOG("\n\tWrite status: %zu\nResponse:\n\"%s\"\n", write_status, req_info->response_h);

	//Did we make progress?
	if (write_status > 0) {
		req_info->progress += write_status;
	}

	//Return on block/error, otherwise go to next stage
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		LOG("Write blocked!\n");
		//Resume request later
		return 0;
	} else if (errno == SIGPIPE) {
		LOG("Sigpipe on %d\n", fd);
		//Ignore request
		return 3;
	} else if (errno != 0) { //SIGPIPE or error
		LOG("Error writing header\n");
		//Ignore request
		return 3;
	} else {
		LOG("Completed writing response!\n");
		req_info->stage += 1;
		req_info->progress = 0;
	}

	return 1;
}			

//return the type of request indicated by the beginning of the header
verb check_verb(char *header) {

	if (strncmp(header, "GET ", 4) == 0) {
		return GET;
	} else if (strncmp(header, "HEAD ", 5) == 0) {
		return HEAD;
	} else if (strncmp(header, "POST ", 5) == 0) {
		return POST;
	} else if (strncmp(header, "PUT ", 4) == 0) {
		return HEAD;
	} else if (strncmp(header, "DELETE ", 7) == 0) {
		return HEAD;
	} else if (strncmp(header, "CONNECT ", 8) == 0) {
		return HEAD;
	} else if (strncmp(header, "OPTIONS ", 8) == 0) {
		return HEAD;
	} else if (strncmp(header, "TRACE ", 6) == 0) {
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

	int fd = 0;
	while ((fd = accept(server_socket, NULL, NULL)) > 0) {

		LOG("Found client\n");

		if (fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("accept");
			LOG("Failed to connect to a client\n");
		} else if (fd >= 0) {
			add_client(fd);
			accept_connections();
			LOG("Accepted a connection on file descriptor %d\n", fd);
		} else {
			errno = 0;
		}
	}
}

void acknowledge_sigpipe(int arg) {
	LOG("SIGPIPE!\n");
}

void graceful_exit(int arg) {
	//free(client_requests);
	//more?
	exit(0);
}
