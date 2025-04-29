#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <getopt.h>
#include "http.h"
#include "get.h"
#include "post.h"
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>

using namespace std;

#define MAXREQ 256
#define N 8192

//1.监听客户端连接；2.与客户端建立连接；3.读取客户端请求；4.处理请求；
void* server(void* args) {
  
  //待补全
  //传进来的参数是服务器套接字句柄（文件描述符）的指针
  int* serversockfdAddr = (int*)args;
  int server_sockfd = *serversockfdAddr;
  
  while(1){
    int consockfd;
    socklen_t sin_size = sizeof(struct sockaddr_in);
    struct sockaddr_in remote_addr;
    //accept时创建一个新的套接字，专门负责和客户端通信
    if(( consockfd = accept(server_sockfd, (struct sockaddr*)&remote_addr, &sin_size)) < 0){ 
      perror("Error: accept");
      continue;
    }

    char* readbuffer = (char*)malloc(N+1);
    int n;
    struct http_request* request = (struct http_request*)malloc(sizeof(struct http_request));
    //从socket中读取信息到用户级的缓冲区
    n=read(consockfd, readbuffer, N);
    if(n < 0){
      perror("Error: read");
      close(consockfd);
      free(readbuffer);
      free(request);
      continue;
    }

//http流水线
//​​​这个 server 函数通过 ​​分块解析请求​​ 和 ​​循环处理缓冲区​​ 的方式支持 HTTP 流水线（Pipeline），以下是具体原因和实现逻辑：
// ​​1. HTTP 流水线（Pipeline）是什么？​​
// HTTP 流水线允许客户端 ​​在一个 TCP 连接上连续发送多个请求​​，而无需等待服务器响应前一个请求。例如：
// GET /file1 HTTP/1.1\r\n\r\n
// GET /file2 HTTP/1.1\r\n\r\n
// POST /upload HTTP/1.1\r\n\r\n...
//服务器必须按顺序解析并响应这些请求。

    char* start_str=readbuffer; // 指向请求数据的起始位置
    char* end_str=NULL; // 用于查找请求结束标记 "\r\n\r\n"

    //strstr用于查找子串，返回该子串起始位置的指针
    while(end_str=strstr(start_str, "\r\n\r\n")){
      size_t len=end_str-start_str+4; //len包含\r\n\r\n
      //这里的request_buf存储请求行
      char* request_buf=(char*)malloc(len+1);
      memcpy(request_buf, start_str, len);
      request_buf[len]='\0';


      if(strstr(request_buf, "POST")){ //若请求方法是post
        char* content_l=strstr(readbuffer, "Content-Length: ");
        int cont_l=0;

        //sscanf用于​从字符串中按格式提取数据​​,content_l是字符串"Content-Length:1234"
        sscanf(content_l, "Content-Length: %d", &cont_l); //%d表示提取一个整数
        //重新计算完整请求长度（头部 + 请求体 +\r\n\r\n）
        len=len+cont_l+4; 

        free(request_buf);
        //这里的request_buf存储请求行+请求体
        request_buf=(char*)malloc(len+1);
        memcpy(request_buf, start_str, len);

        request=http_request_parse(request_buf);
        post_method(request, consockfd);
       
      }else{ //请求方法是get
        request=http_request_parse(request_buf);
        get_method(request, consockfd);
      }
      free(request_buf);
      start_str=end_str+4; // 移动指针到下一个请求
    }
    close(consockfd);
    free(readbuffer);
    free(request);
  }
  return NULL;
}

//1.创建套接字；2.绑定套接字；3.监听套接字；4.创建线程；5.等待线程结束；6.关闭套接字。
int main(int argc, char *argv[])
{
  int option;
  char *ip = "127.0.0.1";
  int port = 8888;
  int threads = 8;
  char *proxy_ip = "127.0.0.1";
  
  //这个结构体数组用于定义 ​​命令行参数的长选项（--option）和短选项（-o）​​，通常与 getopt_long() 函数配合使用，解析用户输入的命令行参数。
  //标准库 <getopt.h> 提供的结构体,定义如下:
//   struct option {
//     const char *name;    // 长选项名称（如 "--ip"）
//     int         has_arg; // 是否需要参数（required_argument, optional_argument, no_argument）
//     int        *flag;    // 一般不使用，设为 0
//     int         val;     // 短选项字符（如 'i'）
// };
  struct option long_options[] = {
      {"ip", required_argument, 0, 'i'},
      {"port", required_argument, 0, 'p'},
      {"threads", required_argument, 0, 't'},
      {"proxy_ip", required_argument, 0, 'x'},
      {0, 0, 0, 0}}; // 结束标记

  //getopt_long() 会遍历 long_options，匹配用户输入的命令行参数
  while ((option = getopt_long(argc, argv, "i:p:t:", long_options, NULL)) != -1)
  {
    switch (option)
    {
      case 'i':
        ip = optarg;
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 't':
        threads = atoi(optarg);
        break;
      case 'x':
        proxy_ip = optarg;
        break;
      default:
        return 1;
        }
    }
   
  //待补全 

  //定义本地地址信息结构体
  struct sockaddr_in myaddr;
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = inet_addr(ip); //inet_addr函数将点分十进制的IP地址转换为网络字节序的整数
  myaddr.sin_port = htons(port);

  //创建套接字
  int server_sockfd;
  if((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    perror("Error: socket");
    return 1;
  }

  //配置：即使不断重启服务器，也不会报错端口被占用
  int on=1;
  if(setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
    perror("Error: setsockopt");
    return 1;
  }

  //绑定套接字
  if(bind(server_sockfd, (struct sockaddr*)&myaddr, sizeof(struct sockaddr_in)) < 0){
    perror("Error: bind");
    return 1;
  }

  //监听套接字
  if(listen(server_sockfd, 10) < 0){
    perror("Error: listen");
    return 1;
  }

  //创建线程
  vector<pthread_t> threadl(threads);
  for(int i=0; i<threads; i++){
    if(pthread_create(&threadl[i], NULL, server, (void*)&server_sockfd) != 0){
      perror("Error: pthread_create failed");
      return 1;
    }
  }

  //等待线程结束
  for(int i=0; i<threads; i++){
    pthread_join(threadl[i], NULL);
  }
  //关闭套接字
  close(server_sockfd);
  
  return 0;
}