#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <asm-generic/socket.h>

#define BUFFER_SIZE 1024
char *response_ok = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s";
char *response_not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s";

int main()
{
	// Disable output buffering
	setbuf(stdout, NULL);

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
	{
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	const int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (client_fd < 0)
	{
		printf("Accept failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Client connected\n");

	char request_buffer[BUFFER_SIZE];

	if (read(client_fd, request_buffer, BUFFER_SIZE) < 0)
	{
		printf("Read failed: %s \n", strerror(errno));
		return 1;
	}
	else
	{
		printf("Request from client: %s\n", request_buffer);
	}

	char *method = strtok(request_buffer, " ");
	char *path = strtok(NULL, " ");

	char response[BUFFER_SIZE];
	char body[BUFFER_SIZE] = {0};
	int length;

	if (strcmp(path, "/") == 0)
	{
		strcpy(body, "OK");
		length = sprintf(response,
						 "HTTP/1.1 200 OK\r\n"
						 "Content-Type: text/plain\r\n"
						 "Content-Length: %d\r\n"
						 "\r\n"
						 "%s",
						 2, body);
	}
	else if (strncmp(path, "/echo/", 6) == 0)
	{
		char *message = path + 6;
		length = sprintf(response,
						 "HTTP/1.1 200 OK\r\n"
						 "Content-Type: text/plain\r\n"
						 "Content-Length: %zu\r\n"
						 "\r\n"
						 "%s",
						 strlen(message), message);
	}
	else
	{
		strcpy(body, "Not Found");
		length = sprintf(response,
						 "HTTP/1.1 404 Not Found\r\n"
						 "Content-Type: text/plain\r\n"
						 "Content-Length: %d\r\n"
						 "\r\n"
						 "%s",
						 9, body);
	}

	if (send(client_fd, response, strlen(response), 0) < 0)
	{
		printf("Send failed: %s\n", strerror(errno));
	}

	close(server_fd);
	close(client_fd);

	return 0;
}
