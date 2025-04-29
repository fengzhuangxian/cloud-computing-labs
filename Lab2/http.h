/*
 * A simple HTTP library.
 *
 * Usage example:
 *
 *     // Returns NULL if an error was encountered.
 *     struct http_request *request = http_request_parse(fd);
 *
 *     ...
 *
 *     http_start_response(fd, 200);
 *     http_send_header(fd, "Content-type", http_get_mime_type("index.html"));
 *     http_send_header(fd, "Server", "httpserver/1.0");
 *     http_end_headers(fd);
 *     http_send_string(fd, "<html><body><a href='/'>Home</a></body></html>");
 *
 *     close(fd);
 */
//这是一个简单的http库，提供了HTTP请求解析和响应发送的功能。
//使用示例如上：
//1. 解析HTTP请求：使用http_request_parse函数解析HTTP请求，返回一个http_request结构体指针。
//2. 发送HTTP响应：使用http_start_response函数开始发送响应，指定状态码；使用http_send_header函数发送响应头；使用http_end_headers函数结束响应头；使用http_send_string函数发送响应体。
//3. 关闭文件描述符：使用close函数关闭文件描述符。


 #ifndef HTTP_H
 #define HTTP_H
 
 /*
  * Functions for parsing an HTTP request.
  */
 struct http_request
 {
     char *method;
     char *path;
     char *content_type;
     char *content;
 };
 //http请求结构体，包含请求方法、请求路径、内容类型和内容。
 // request->method = "GET"
 // request->path = "/index.html"
 // request->content_type = "text/html" (如果是 POST 请求)
 // request->content = "..." (如果是 POST 请求)

 struct http_request *http_request_parse(char* read_buffer);
 
 /*
  * Functions for sending an HTTP response.
  */
 void http_start_response(int fd, int status_code);
 void http_send_header(int fd, char *key, char *value);
 void http_end_headers(int fd);


 //格式化超链接（用于生成 HTML 目录列表）
 void http_format_href(char *buffer, char *path, char *filename);

 //生成目录索引页面的 HTML（用于 GET / 请求）。
 void http_format_index(char *buffer, char *path);
 
 /*
  * Helper function: gets the Content-Type based on a file name.
  */
 char *http_get_mime_type(char *file_name);
 
 //等待 socket 数据可读（可能用于非阻塞 I/O 或超时处理）
 void wait_for_data(int fd);
 
 #endif