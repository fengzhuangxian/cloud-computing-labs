#include "message_handler.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>

namespace raft {

// 从socket读取数据，尝试提取完整的Raft消息
std::pair<bool, std::vector<std::unique_ptr<Message>>> MessageHandler::readRaftMessages(int sockfd, std::string& buffer) {
    // 分配临时缓冲区
    char temp_buffer[raft::MAX_BUFFER_SIZE];
    
    // 从socket读取数据
    ssize_t n = recv(sockfd, temp_buffer, sizeof(temp_buffer), 0);
    if (n <= 0) {
        return std::make_pair(false, std::vector<std::unique_ptr<Message>>());  // 连接已关闭或出错
    }
    
    // 将新接收的数据追加到持久化缓冲区
    buffer.append(temp_buffer, n);
    
    // 处理标准的Raft内部消息
    try {
        auto messages = processRaftBuffer(buffer);
        return std::make_pair(true, std::move(messages));
    } catch (const std::exception& e) {
        std::cerr << "Raft消息解析错误: " << e.what() << std::endl;
        return std::make_pair(false, std::vector<std::unique_ptr<Message>>());
    }
}

// 从socket读取数据，尝试提取完整的客户端请求
std::pair<bool, std::string> MessageHandler::readClientRequest(int sockfd, std::string& buffer) {
    // 分配临时缓冲区
    char temp_buffer[raft::MAX_BUFFER_SIZE];
    
    // 从socket读取数据
    ssize_t n = recv(sockfd, temp_buffer, sizeof(temp_buffer), 0);
    if (n <= 0) {
        return std::make_pair(false, std::string());  // 连接已关闭或出错
    }
    
    // 将新接收的数据追加到持久化缓冲区
    buffer.append(temp_buffer, n);
    
    // 处理客户端请求
    return processClientBuffer(buffer);
}

// 向socket发送一条Raft消息
bool MessageHandler::sendRaftMessage(int sockfd, const Message& message) {
    // 创建完整的网络消息
    std::string network_message = message.createNetworkMessage();
    
    // 发送消息
    ssize_t sent = 0;
    size_t total_size = network_message.size();
    
    while (sent < static_cast<ssize_t>(total_size)) {
        ssize_t n = send(sockfd, network_message.c_str() + sent, total_size - sent, 0);
        if (n <= 0) {
            return false;  // 发送失败
        }
        sent += n;
    }
    
    return true;
}

// 处理Raft消息接收缓冲区，尝试提取完整消息
std::vector<std::unique_ptr<Message>> MessageHandler::processRaftBuffer(std::string& buffer) {
    std::vector<std::unique_ptr<Message>> messages;
    
    while (buffer.size() >= sizeof(MessageHeader)) {
        const MessageHeader* header = reinterpret_cast<const MessageHeader*>(buffer.data());
        size_t message_size = sizeof(MessageHeader) + header->payload_size;
        
        // 检查是否有完整的消息
        if (buffer.size() < message_size) {
            break;  // 消息不完整，等待更多数据
        }
        
        // 提取并解析消息
        try {
            std::string message_data = buffer.substr(0, message_size);
            auto message = parseMessage(message_data);
            messages.push_back(std::move(message));
        } catch (const std::exception& e) {
            std::cerr << "消息解析错误: " << e.what() << std::endl;
        }
        
        // 移除已处理的消息
        buffer.erase(0, message_size);
    }
    
    return messages;
}

// 处理客户端请求接收缓冲区，尝试提取完整请求
std::pair<bool, std::string> MessageHandler::processClientBuffer(std::string& buffer) {
    // RESP协议通常以*或$开头
    if (buffer.empty() || (buffer[0] != '*' && buffer[0] != '$')) {
        return std::make_pair(false, "");
    }
    
    // 简单检测RESP协议是否完整 (确保包含完整的\r\n分隔符)
    size_t pos = 0;
    int args_count = 0;
    
    if (buffer[0] == '*') {
        // 批量字符串数组
        size_t end_pos = buffer.find("\r\n", 1);
        if (end_pos == std::string::npos) {
            return std::make_pair(false, "");  // 不完整的命令
        }
        
        try {
            args_count = std::stoi(buffer.substr(1, end_pos - 1));
        } catch (const std::exception&) {
            return std::make_pair(false, "");  // 无效的参数计数
        }
        
        pos = end_pos + 2;  // 跳过\r\n
        
        // 检查是否包含所有参数
        for (int i = 0; i < args_count; ++i) {
            if (pos >= buffer.length() || buffer[pos] != '$') {
                return std::make_pair(false, "");  // 命令格式错误
            }
            
            // 查找参数长度
            end_pos = buffer.find("\r\n", pos + 1);
            if (end_pos == std::string::npos) {
                return std::make_pair(false, "");  // 不完整的命令
            }
            
            int arg_len;
            try {
                arg_len = std::stoi(buffer.substr(pos + 1, end_pos - pos - 1));
            } catch (const std::exception&) {
                return std::make_pair(false, "");  // 无效的参数长度
            }
            
            pos = end_pos + 2;  // 跳过\r\n
            
            // 检查参数值是否完整
            if (pos + arg_len + 2 > buffer.length()) {
                return std::make_pair(false, "");  // 命令不完整
            }
            
            // 移动到下一个参数
            pos += arg_len + 2;  // +2跳过\r\n
        }
    } else if (buffer[0] == '$') {
        // 单个批量字符串
        size_t end_pos = buffer.find("\r\n", 1);
        if (end_pos == std::string::npos) {
            return std::make_pair(false, "");  // 不完整的命令
        }
        
        int str_len;
        try {
            str_len = std::stoi(buffer.substr(1, end_pos - 1));
        } catch (const std::exception&) {
            return std::make_pair(false, "");  // 无效的字符串长度
        }
        
        pos = end_pos + 2;  // 跳过\r\n
        
        // 检查字符串是否完整
        if (pos + str_len + 2 > buffer.length()) {
            return std::make_pair(false, "");  // 字符串不完整
        }
        
        pos += str_len + 2;  // +2跳过\r\n
    }
    
    // 命令完整，返回整个缓冲区
    std::string command = buffer.substr(0, pos);
    buffer.erase(0, pos);  // 从缓冲区移除已处理的命令
    return std::make_pair(true, command);
}

// 发送客户端响应
bool MessageHandler::sendClientResponse(int sockfd, const std::string& response) {
    ssize_t sent = 0;
    size_t total_size = response.size();
    
    while (sent < static_cast<ssize_t>(total_size)) {
        ssize_t n = send(sockfd, response.c_str() + sent, total_size - sent, 0);
        if (n <= 0) {
            return false;  // 发送失败
        }
        sent += n;
    }
    
    return true;
}

} // namespace raft 