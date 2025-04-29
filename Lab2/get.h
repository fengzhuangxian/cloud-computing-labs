#ifndef GET_H
#define GET_H

int get_file_length(char *path);

int get_status_of_the_file(char *path);

void get_method(struct http_request *request, int server_socket);

void echo_back(struct http_request *request, int server_socket, int status);
#endif // GET_H