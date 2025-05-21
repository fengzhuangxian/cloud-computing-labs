#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include <string>
#include <vector>
#include <memory>
#include <queue>
#include <stdexcept>
#include "message.h"
#include "../include/constants.h"

namespace raft {

// 消息处理类，负责消息的封装和解析
class MessageHandler {
public:
    // 从socket读取数据，尝试提取完整的Raft消息
    static std::pair<bool, std::vector<std::unique_ptr<Message>>> readRaftMessages(int sockfd, std::string& buffer);
    // 从socket读取数据，尝试提取完整的客户端请求
    static std::pair<bool, std::string> readClientRequest(int sockfd, std::string& buffer);
    
    // 向socket发送一条Raft消息
    static bool sendRaftMessage(int sockfd, const Message& message);
    // 向socket发送一条客户端响应
    static bool sendClientResponse(int sockfd, const std::string& response);
    
    
    // 处理Raft消息接收缓冲区，尝试提取完整消息
    static std::vector<std::unique_ptr<Message>> processRaftBuffer(std::string& buffer);
    // 处理客户端请求接收缓冲区，尝试提取完整请求
    static std::pair<bool, std::string> processClientBuffer(std::string& buffer);
};

} // namespace raft

#endif // MESSAGE_HANDLER_H 