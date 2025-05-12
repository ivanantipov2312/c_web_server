#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>

#define PORT "8080"
#define BACKLOG 15

struct file_data {
	char *data;
	size_t len;
};

struct file_data *read_file(const char *filepath)
{
	struct file_data *data = malloc(sizeof *data);
	FILE *fp = fopen(filepath, "rb");

	// If file doesn't exist
	if (fp == NULL) {
		fprintf(stderr, "Error openning file!\n");
		return NULL;
	}

	// Get file length
	fseek(fp, 0, SEEK_END);
	data->len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	data->data = malloc(data->len + 1);
	if (data->data) {
		// Read file contents into buffer
		fread(data->data, 1, data->len, fp);
		data->data[data->len] = '\0';
	}

	return data;
}

char *get_mime_type(const char *ext)
{
	if (strcmp(ext, "html") == 0)
		return "text/html";
	if (strcmp(ext, "css") == 0)
		return "text/css";
	if (strcmp(ext, "js") == 0)
		return "text/javascript";
	if (strcmp(ext, "txt") == 0)
		return "text/plain";
	return NULL;
}

char *send_file_response(char *filename, const char *ext)
{
	char *resp = malloc(512);
	struct file_data *f = read_file(filename);
	char *mime = get_mime_type(ext);

	if (f == NULL) { // If file doesn't exist
		f = read_file("404.html");
	}

	// Construct our response
	sprintf(resp, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n%s", mime, f->len, f->data);
	size_t resp_len = strlen(resp);
	resp[resp_len] = '\0';

	return resp;
}

void *http_thread(void *arg)
{
	int newfd = *((int *)arg);

	// Recieve method
	char req[128];
	if (recv(newfd, req, 128, 0) == -1) {
		perror("recv");
		return NULL;
	}

	// Extract method name and filename
	char *command = strtok(req, "/");
	char *subcommand = strtok(NULL, "/");
	subcommand = strtok(subcommand, " "); // Delete whitespaces
	if (strcmp(command, "GET ") == 0) { // If method is GET
		char *resp = NULL;
		if (strcmp(subcommand, "HTTP") == 0) { // GET /
			resp = send_file_response("index.html", "html");
		} else {
			char filename[32];
			sprintf(filename, "%s.html", subcommand); // Find file something.html and send it
			resp = send_file_response(filename, "html");
		}

		size_t resp_len = strlen(resp);
		send(newfd, resp, resp_len, 0); // Send our response
	}

	close(newfd); // Close connection socket
	return NULL;
}

int get_listener_socket(void)
{
	int sockfd;

	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // Use TCP
	hints.ai_flags = AI_PASSIVE; // Use my IP

	struct addrinfo *servinfo;
	int status;
	if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) == -1) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return -1;
	}

	struct addrinfo *p;
	int yes = 1;
	for (p = servinfo; p != NULL; p = p->ai_next) {
		// Create new socket
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}

		// Get rid of 'address already in use' error message
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			continue;
		}

		// Bind the socket to IP
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("bind");
			continue;
		}

		break; // Get first address that passed all the checks
	}

	freeaddrinfo(servinfo);

	if (p == NULL) {
		fprintf(stderr, "Failed to connect!\n");
		return -1;
	}

	// Set socket to listening
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		return -1;
	}

	return sockfd;
}

int main(void)
{
	int sockfd;
	if ((sockfd = get_listener_socket()) == -1) {
		fprintf(stderr, "Failed to create listener socket!\n");
		return -1;
	}

	while (1) {
		struct sockaddr_storage client;
		socklen_t sin_size = sizeof client;

		int *newfd = malloc(sizeof(int));
		if ((*newfd = accept(sockfd, (struct sockaddr *)&client, &sin_size)) == -1) {
			continue;
		}

		// Create new thread, give our socket to it and detach it
		pthread_t accept_thread;
		pthread_create(&accept_thread, NULL, http_thread, (void *)newfd);
		pthread_detach(accept_thread);
	}

	close(sockfd);
	return 0;
}
