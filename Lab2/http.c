#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "http.h"

#define DEBUG 1

#define LIBHTTP_REQUEST_MAX_SIZE 8192

//输出错误信息并终止程序。
void http_fatal_error(char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(ENOBUFS);
}

//http_request_parse 函数解析 HTTP 请求，区分 GET 和 POST 请求，提取请求方法、路径、内容类型和内容。
struct http_request *http_request_parse(char* read_buffer)
{
    struct http_request *request = (struct http_request *)malloc(sizeof(struct http_request));
    if (!request)
        http_fatal_error("Malloc failed");
    if(strstr(read_buffer, "GET")){
        request->method = (char*)malloc(20);
        request->path = (char*)malloc(30);
        sscanf(read_buffer, "%s %s", request->method, request->path);
    }else {
        request->method = (char*)malloc(10); request->path = (char*)malloc(20);
        sscanf(read_buffer, "%s %s", request->method, request->path);
        //content-type
        request->content_type = (char*)malloc(50);
        char* content_type = strstr(read_buffer, "Content-Type: ");
        sscanf(content_type, "Content-Type: %s", request->content_type);
        int cont_l = 0;
        char* content_l = strstr(read_buffer, "Content-Length: ");
        sscanf(content_l, "Content-Length: %d", &cont_l);
        request->content = (char*)malloc(cont_l + 10);
        char* content = strstr(read_buffer, "\r\n\r\n") + 4; // 要从下一行开始
        memcpy(request->content, content, cont_l);
        request->content[cont_l] = '\0';
    }
    return request;
}

//http_get_response_message 函数根据状态码返回对应的 HTTP 响应消息。
char *http_get_response_message(int status_code)
{
    switch (status_code)
    {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Internal Server Error";
    }
}

//http_start_response 函数发送 HTTP 响应的起始行。
// http_send_header 函数发送 HTTP 响应头。
// http_end_headers 函数结束 HTTP 响应头的发送。
void http_start_response(int fd, int status_code)
{
    dprintf(fd, "HTTP/1.1 %d %s\r\n", status_code, http_get_response_message(status_code));
}

void http_send_header(int fd, char *key, char *value) { dprintf(fd, "%s: %s\r\n", key, value); }

void http_end_headers(int fd) { dprintf(fd, "\r\n"); }

//http_get_mime_type 函数根据文件名返回对应的 MIME 类型。
char *http_get_mime_type(char *file_name)
{
    char *file_extension = strrchr(file_name, '.');
    if (file_extension == NULL)
    {
        return "text/plain";
    }

    if (strcmp(file_extension, ".html") == 0 || strcmp(file_extension, ".htm") == 0)
    {
        return "text/html";
    }
    else if (strcmp(file_extension, ".jpg") == 0 || strcmp(file_extension, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    else if (strcmp(file_extension, ".png") == 0)
    {
        return "image/png";
    }
    else if (strcmp(file_extension, ".css") == 0)
    {
        return "text/css";
    }
    else if (strcmp(file_extension, ".js") == 0)
    {
        return "text/javascript";
    }
    else if (strcmp(file_extension, ".pdf") == 0)
    {
        return "application/pdf";
    }
    else if (strcmp(file_extension, ".json") == 0)
    {
        return "application/json";
    }
    else
    {
        return "text/plain";
    }
}

/*
 * Puts `<a href="/path/filename">filename</a><br/>` into the provided buffer.
 * The resulting string in the buffer is null-terminated. It is the caller's
 * responsibility to ensure that the buffer has enough space for the resulting string.
 */
 //http_format_href 函数生成 HTML 超链接。
// http_format_index 函数生成索引文件路径。
void http_format_href(char *buffer, char *path, char *filename)
{
    int length = strlen("<a href=\"//\"></a><br/>") + strlen(path) + strlen(filename) * 2 + 1;
    snprintf(buffer, length, "<a href=\"/%s/%s\">%s</a><br/>", path, filename, filename);
}

/*
 * Puts `path/index.html` into the provided buffer.
 * The resulting string in the buffer is null-terminated.
 * It is the caller's responsibility to ensure that the
 * buffer has enough space for the resulting string.
 */
void http_format_index(char *buffer, char *path)
{
    int length = strlen(path) + strlen("/index.html") + 1;
    snprintf(buffer, length, "%s/index.html", path);
}

//wait_for_data 函数等待套接字上的数据，设置超时时间为 5 秒。
void wait_for_data(int fd){
    struct timeval timeout = {5, 0};
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("Failed to get socket flags");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set socket to non-blocking mode");
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    int select_result = select(fd + 1, &read_fds, NULL, NULL, &timeout);
    if (select_result == -1) {
        perror("Failed to select on socket");
        exit(EXIT_FAILURE);
    } else if (select_result == 0) {
        printf("No data received within 5 seconds, closing socket.\n");
        close(fd);
        exit(EXIT_SUCCESS);
    }
}