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
char *response_created = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
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

void handle_new_connection(int server_fd)
{
	struct sockaddr_in client_addr;
	int client_addr_len = sizeof(client_addr);
	printf("Waiting for a client to connect...\n");
	while (1)
	{
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
			process_client_request(client_fd);
			exit(0);
		}
		else
		{
			close(client_fd);
		}
	}
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

void handle_post_request(int client_fd, char *path, char *request_buffer, ssize_t bytes_read)
{
	char *filename = path + 7; // Skip past "/files/"
	char full_path[BUFFER_SIZE];
	snprintf(full_path, sizeof(full_path), "%s/%s", directory, filename);

	printf("Buffer Request: %s\n", request_buffer);
	long content_length = 0;
	char *content_length_str_start = strstr(request_buffer, "Content-Length: ");
	if (content_length_str_start)
	{
		content_length = atol(content_length_str_start + 16);
	}

	FILE *file = fopen(full_path, "w");
	if (!file)
	{
		send(client_fd, response_internal_error, strlen(response_internal_error), 0);
		return;
	}

	char *body_start = strstr(request_buffer, "\r\n\r\n") + 4;
	ssize_t body_read = bytes_read - (body_start - request_buffer);
	if (body_read > 0 && body_read <= content_length)
	{
		fwrite(body_start, 1, body_read, file);
		content_length -= body_read;
	}

	printf("Content length: %ld\n", content_length);
	char buffer[BUFFER_SIZE];
	while (content_length > 0)
	{
		ssize_t bytes_to_read = sizeof(buffer) < content_length ? sizeof(buffer) : content_length;
		ssize_t read_result = read(client_fd, buffer, bytes_to_read);
		if (read_result > 0)
		{
			fwrite(buffer, 1, read_result, file);
			content_length -= read_result;
		}
		else if (read_result < 0)
		{
			printf("Error reading from socket: %s\n", strerror(errno));
			break;
		}
	}

	fclose(file);
	send(client_fd, response_created, 44, 0);
}

void process_client_request(int client_fd)
{
	char request_buffer[BUFFER_SIZE];
	memset(request_buffer, 0, sizeof(request_buffer));
	ssize_t total_bytes_read = 0;
	ssize_t bytes_read = read(client_fd, request_buffer, BUFFER_SIZE - 1);
	if (bytes_read < 0)
	{
		printf("Read failed: %s\n", strerror(errno));
		return;
	}
	total_bytes_read += bytes_read;

	while (!strstr(request_buffer, "\r\n\r\n") && total_bytes_read < sizeof(request_buffer) - 1)
	{
		bytes_read = read(client_fd, request_buffer + total_bytes_read, BUFFER_SIZE - total_bytes_read);
		if (bytes_read < 0)
		{
			printf("Read failed: %s\n", strerror(errno));
			return;
		}
		else if (bytes_read == 0)
		{
			break;
		}
		total_bytes_read += bytes_read;
	}

	printf("Request from client: %s\n", request_buffer);

	char request_buffer_copy[BUFFER_SIZE];
	strcpy(request_buffer_copy, request_buffer);

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

	if (strcmp(path, "/") == 0)
	{
		char response[BUFFER_SIZE];
		int length = sprintf(response, response_ok, 2, "OK");
		send(client_fd, response, length, 0);
	}
	else if (strncmp(path, "/echo/", 6) == 0)
	{
		char *message = path + 6;
		char response[BUFFER_SIZE];
		int length = sprintf(response, response_ok, strlen(message), message);
		send(client_fd, response, length, 0);
	}
	else if (strcmp(path, "/user-agent") == 0 && strlen(user_agent) > 0)
	{
		char response[BUFFER_SIZE];
		int length = sprintf(response, response_ok, strlen(user_agent), user_agent);
		send(client_fd, response, length, 0);
	}
	else if (strcmp(method, "POST") == 0 && strncmp(path, "/files/", 7) == 0)
	{
		handle_post_request(client_fd, path, request_buffer_copy, bytes_read);
	}
	else if (strncmp(path, "/files/", 7) == 0)
	{
		char file_path[BUFFER_SIZE];
		snprintf(file_path, BUFFER_SIZE, "%s%s", directory, path + 6);
		send_file_content(client_fd, file_path);
	}
	else
	{
		char response[BUFFER_SIZE];
		sprintf(response, response_not_found, 13, "File not found");
		send(client_fd, response, strlen(response), 0);
	}

	close(client_fd);
}

int main(int argc, char *argv[])
{
	// Disable output buffering
	setbuf(stdout, NULL);

	directory = argv[2];

	int client_addr_len;
	struct sockaddr_in client_addr;

	// Port is binded to 4221
	int server_fd = init_server(4221);
	if (server_fd < 0)
	{
		printf("Failed to initialize server\n");
		return 1;
	}

	printf("Server listening on port %d\n", 4221);
	handle_new_connection(server_fd);

	close(server_fd);
	return 0;
}
