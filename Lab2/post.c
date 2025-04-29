#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "http.h"
#include "get.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>
#define N 2048
// post_method 函数依据请求路径处理不同的 POST 请求。
// 对于 /api/echo 路径，使用正则表达式验证请求内容，若验证通过则返回 200 响应，否则返回 404 错误。
// 对于 /api/upload 路径，返回 200 响应并发送请求内容。
// 若请求路径不匹配，返回 404 错误。

void post_method(struct http_request *request, int server_socket)
{
    //regex_t regex：定义了一个 regex_t 类型的变量 regex，用于存储编译后的正则表达式。
// regmatch_t matches[3]：定义了一个 regmatch_t 类型的数组 matches，长度为 3，用于存储正则表达式匹配的结果。
    regex_t regex;
    regmatch_t matches[3];
    // 使用 strcmp 函数比较 request->path（请求的路径）和字符串 "/api/echo" 是否相等。如果相等，说明客户端请求的是 /api/echo 接口，进入该 if 语句块进行处理。
    if(!strcmp(request->path, "/api/echo")){
        //调用 regcomp 函数来编译一个正则表达式
        int reti = regcomp(&regex, "id=[0-9]+&name=[A-Za-z0-9]+", REG_EXTENDED);
        if(reti) {
            perror("re compile error");
            exit(1);
        }
        // regexec 函数来执行正则表达式匹配
        reti = regexec(&regex, request->content, 3, matches, 0);
        //如果 reti 不为 0，说明正则表达式匹配失败。
        // strcpy(request->path, "./data/error.txt")：将 request->path 的值修改为 "./data/error.txt"。
        // echo_back(request, server_socket, 404)：调用 echo_back 函数，将错误信息返回给客户端，并返回 HTTP 状态码 404（表示未找到资源）。
        if(reti){
            strcpy(request->path, "./data/error.txt");
            echo_back(request, server_socket, 404);
        }else {
            //http_start_response(server_socket, 200)：调用 http_start_response 函数，开始向客户端发送 HTTP 响应，状态码为 200（表示请求成功）。
            http_start_response(server_socket, 200);
            //调用 http_send_header 函数，向客户端发送 HTTP 响应头，指定响应的内容类型为 request->content_type。
            http_send_header(server_socket, "Content-Type", request->content_type);
            char *content_len = (char *)malloc(30);
            //http_send_header(server_socket, "Content-Length", content_len)：向客户端发送 HTTP 响应头，指定响应的内容长度。
            snprintf(content_len, 30, "%lu", strlen(request->content));
            http_send_header(server_socket, "Content-Length", content_len);
            free(content_len);
            //调用 http_end_headers 函数，结束 HTTP 响应头的发送。
            http_end_headers(server_socket);
            //调用 write 函数，将请求内容返回给客户端。
            write(server_socket, request->content, strlen(request->content));
        }
    //upload
    }else if(!strcmp(request->path, "/api/upload")){
        printf("upload\n");
        http_start_response(server_socket, 200);
        http_send_header(server_socket, "Content-Type", http_get_mime_type(".json"));
        char *content_len = (char *)malloc(30);
        snprintf(content_len, 30, "%lu", strlen(request->content));
        http_send_header(server_socket, "Content-Length", content_len);
        free(content_len);
        http_end_headers(server_socket);
        write(server_socket, request->content, strlen(request->content));  
    }else {
            strcpy(request->path, "./static/404.html");
            echo_back(request, server_socket, 404);
        }
}