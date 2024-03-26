#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <asm-generic/socket.h>

#define BUFFER_SIZE 4096
char *response_ok = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s";
char *response_ok_unknown_type = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n";
char *response_not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s";
char *response_internal_error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
char *directory;

int init_server(int port);
void handle_new_connection(int server_fd);
void send_file_content(int client_fd, const char *file_path);
void process_client_request(int client_fd);

int init_server(int port)
{
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
	{
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
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

	return server_fd;
}

void send_file_content(int client_fd, const char *file_path)
{
	FILE *file = fopen(file_path, "r");
	if (!file)
	{
		char buffer[BUFFER_SIZE];
		sprintf(buffer, response_not_found, 13, "File not found");
		send(client_fd, buffer, strlen(buffer), 0);
		return;
	}

	// Go to end of file and get file size
	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	// return to file start and start reading
	rewind(file);

	char header[BUFFER_SIZE];
	int header_len = sprintf(header, response_ok_unknown_type, file_size);
	send(client_fd, header, header_len, 0);

	char buffer[BUFFER_SIZE];
	size_t bytes_read;
	while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0)
	{
		send(client_fd, buffer, bytes_read, 0);
	}

	fclose(file);
}

int main(int argc, char *argv[])
{
	// Disable output buffering
	setbuf(stdout, NULL);

	directory = argv[2];

	int client_addr_len;
	struct sockaddr_in client_addr;

	// Port is hardcoded to 4221
	int server_fd = init_server(4221);

	printf("Waiting for a client to connect...\n");
	while (1)
	{
		client_addr_len = sizeof(client_addr);

		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd < 0)
		{
			printf("Accept failed: %s \n", strerror(errno));
			continue;
		}

		int pid = fork();
		if (pid < 0)
		{
			printf("Fork failed: %s\n", strerror(errno));
			close(client_fd);
		}
		else if (pid == 0)
		{
			close(server_fd);
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

			char *headerLine = NULL;
			char user_agent[BUFFER_SIZE] = {0};

			while ((headerLine = strtok(NULL, "\r\n")) && *headerLine)
			{
				if (strncmp(headerLine, "User-Agent:", 11) == 0)
				{
					strncpy(user_agent, headerLine + 12, sizeof(user_agent) - 1);
					break;
				}
			}

			char response[BUFFER_SIZE];
			char body[BUFFER_SIZE] = {0};
			int length;

			if (strcmp(path, "/") == 0)
			{
				length = sprintf(response, response_ok, 2, "OK");
			}
			else if (strncmp(path, "/echo/", 6) == 0)
			{
				char *message = path + 6;
				length = sprintf(response, response_ok, strlen(message), message);
			}
			else if (strcmp(path, "/user-agent") == 0 && strlen(user_agent) > 0)
			{
				length = sprintf(response, response_ok, strlen(user_agent), user_agent);
			}
			else if (strncmp(path, "/files/", 7) == 0)
			{
				char file_path[BUFFER_SIZE];
				snprintf(file_path, BUFFER_SIZE, "%s%s", directory, path + 6);
				send_file_content(client_fd, file_path);
				close(client_fd);
				exit(0);
			}
			else
			{
				length = sprintf(response, response_not_found, 9, body);
			}

			if (send(client_fd, response, length, 0) < 0)
			{
				printf("Send failed: %s\n", strerror(errno));
			}

			close(client_fd);
			exit(0);
		}
		else
		{
			close(client_fd);
		}
	}

	return 0;
}
