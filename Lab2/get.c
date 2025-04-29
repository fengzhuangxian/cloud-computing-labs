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

//定义 JSON 对象数组和 ID 映射名称数组。
char *jsonobj[4] = {"[{\"id\":1,\"name\":\"Foo\"}]",
                    "[{\"id\":2,\"name\":\"Bar\"}]",
                    "[{\"id\":3,\"name\":\"Foo\"}]",
                    "[{\"id\":4,\"name\":\"Bar\"}]"};

char *id_map_name[4] = {"Foo", "Bar", "Foo", "Bar"};

//get_file_length 函数计算文件的长度。
int get_file_length(char *path)
{
    int fd = open(path, O_RDONLY);
    off_t file_size = 0; // 文件大小
    char buffer;         // 每次读一个字节
    while (read(fd, &buffer, 1) > 0)
    {
        file_size++;
    }
    close(fd);
    return file_size;
}

//get_status_of_the_file 函数根据文件路径返回对应的 HTTP 状态码。
int get_status_of_the_file(char *path)
{
    if (strstr(path, "404"))
        return 404;
    else if (strstr(path, "403"))
        return 403;
    else if (strstr(path, "501"))
        return 501;
    else if (strstr(path, "502"))
        return 502;
    else
        return 200;
}

//echo_back 函数根据状态码发送 HTTP 响应，若状态码为 201，则返回 JSON 对象，否则返回文件内容。
void echo_back(struct http_request *request, int server_socket, int status)
{
    char reqbuf[N];
    int nread, nwrite;
    char directory[50];
    strcpy(directory, request->path);
    // ----
    if(status == 201) {
        http_start_response(server_socket, 200);
        http_send_header(server_socket, "Content-Type", http_get_mime_type("data.json"));
        int id = atoi(request->path); // 对应的id号
        int len = strlen(jsonobj[id - 1]);
        char *len_s =(char *)malloc(10);
        snprintf(len_s, 10, "%d", len);
        http_send_header(server_socket, "Content-Length", len_s);
        http_end_headers(server_socket);
        nwrite = write(server_socket, jsonobj[id - 1], len);
        free(len_s);
        return ;
    }
    http_start_response(server_socket, status);
    http_send_header(server_socket, "Content-Type", http_get_mime_type(request->path));
    int file_size = get_file_length(request->path);
    char *len = (char *)malloc(10);
    snprintf(len, 10, "%d", file_size);
    http_send_header(server_socket, "Content-Length", len);
    http_end_headers(server_socket);
    // ---- http header
    //---
    int fp = open(directory, O_RDONLY);
    memset(reqbuf, 0, sizeof(reqbuf));
    nread = read(fp, reqbuf, sizeof(reqbuf));
    nwrite = write(server_socket, reqbuf, nread);
    //--- write file in socket
    close(fp);
    free(len);
}

//turn_to_api 函数处理 API 请求，根据请求路径进行不同的处理，使用正则表达式验证 search 请求的参数。
void turn_to_api(struct http_request *request, int server_socket)
{
    int status = 200;
    char apicheck[50] = "./data/data.txt";
    char apilist[50] = "./data/data.json";
    char errorpage[50] = "./static/404.html";
    char apinotfound[50] = "./data/not_found.json";
    if (!strcmp(request->path, "/api/check"))
    {
        strcpy(request->path, apicheck);
    }
    else if (!strcmp(request->path, "/api/list"))
    {
        strcpy(request->path, apilist);
    }
    else if (strstr(request->path, "search?"))
    {
        // 使用正则表达式来匹配id和name
        regex_t regex;
        regmatch_t matches[3];
        int reti = regcomp(&regex, "\\?id=([0-9]+)&name=([A-Za-z0-9]+)", REG_EXTENDED);
        // regex用来存储编译后的信息 0表示不加任何编译选项 编译成功返回0
        if (reti)
        {
            // 防御性编程 编译失败
            perror("regex compile error");
            exit(1);
        }
        reti = regexec(&regex, request->path, 3, matches, 0);
        // 匹配 匹配成功返回0 2表示最大匹配数 matches表示匹配到的子字符串在原字符串中的起止位置
        if (!reti)
        {
            // int nwrite = write(STDOUT_FILENO, "match secceed ", strlen("match secceed "));
            int id_len = matches[1].rm_eo - matches[1].rm_so;
            int name_len = matches[2].rm_eo - matches[2].rm_so;
            char s_id[id_len + 1], name[name_len + 1];
            strncpy(s_id, &request->path[matches[1].rm_so], id_len);
            strncpy(name, &request->path[matches[2].rm_so], name_len);
            s_id[id_len] = '\0';
            name[name_len] = '\0';
            int id = atoi(s_id);// 获得对应的id
            if(id > 4 || (strcmp(name, id_map_name[id - 1]))) { // 如果id >=4 或者name与id对应的字符串不匹配 则就是notfound
                printf("apinotfound");
                strcpy(request->path, apinotfound);
                status = 404;
            }else {
                status = 201;
                strcpy(request->path, s_id);
            }
        }
        else
        {//如果在search的阶段中匹配失败，那肯定就是错误的路径所以返回404
            // printf("search的阶段中匹配失败\n");
            status = 404;
            strcpy(request->path, errorpage);
        }
        regfree(&regex);
    }else {
        //错误路径
        status = 404;
        strcpy(request->path, errorpage);
    }
    echo_back(request, server_socket, status);

    // http:localhost:8888/api/check => ./data/data.txt
}

//get_method 函数处理 GET 请求，若请求路径包含 api，则调用 turn_to_api 处理，否则处理静态文件请求。
void get_method(struct http_request *request, int server_socket)
{
    int n = write(STDOUT_FILENO, request->path, strlen(request->path));
    n = write(STDOUT_FILENO, " ", 1);
    if (strstr(request->path, "api"))
    {
        // printf("请求路径: %s\n", request->path);
        // printf("turn_to_api\n");
        turn_to_api(request, server_socket); // 处理api系列的响应
        return;                              // 处理完毕
    }
    // 其余正常情况
    struct stat file_info;
    int status = 200;
    char directory[50] = "./static";
    strcat(directory, request->path);
    strcpy(request->path, directory);
    if (!strcmp(request->path, "./static/")) // 处理http:localhost:8888/ 返回index.html
    {
        strcpy(directory, "./static/index.html");
        strcpy(request->path, directory);
    }
    else if (stat(directory, &file_info) < 0) // file not exits
    {
        status = 404;
        strcpy(directory, "./static/404.html");
        strcpy(request->path, directory);
    }
    else
        status = get_status_of_the_file(directory); // 正常情况
    echo_back(request, server_socket, status);
}