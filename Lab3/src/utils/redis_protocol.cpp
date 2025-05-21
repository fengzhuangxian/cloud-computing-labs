#include "utils/redis_protocol.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace raft {

std::string RedisProtocol::encode(const std::string& value) {
    if (value.empty()) {
        return "$0\r\n\r\n";
    }
    return "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
}

std::string RedisProtocol::encodeNull() {
    return "$-1\r\n";
}

std::string RedisProtocol::encodeJson(const nlohmann::json& json) {
    std::string jsonStr = json.dump();
    return "$" + std::to_string(jsonStr.length()) + "\r\n" + jsonStr + "\r\n";
}

std::string RedisProtocol::encodeError(const std::string& error) {
    return "-" + error + "\r\n";
}

std::string RedisProtocol::encodeStatus(const std::string& status) {
    return "+" + status + "\r\n";
}

std::string RedisProtocol::encodeInteger(int value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string RedisProtocol::encodeArray(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += encode(item);
    }
    return result;
}

std::string RedisProtocol::decode(const std::string& data) {
    if (data.empty()) {
        return "";
    }
    
    // 处理空值
    if (data == "$-1\r\n") {
        return "";
    }
    
    // 处理大容量字符串
    if (data[0] == '$') {
        size_t firstBreak = data.find("\r\n");
        if (firstBreak == std::string::npos) {
            return "";
        }
        
        std::string lengthStr = data.substr(1, firstBreak - 1);
        int length;
        try {
            length = std::stoi(lengthStr);
        } catch (const std::exception&) {
            return "";
        }
        
        if (length < 0) {
            return "";
        }
        
        size_t contentStart = firstBreak + 2;
        if (contentStart >= data.length()) {
            return "";
        }
        
        size_t contentEnd = contentStart + length;
        if (contentEnd > data.length()) {
            return "";
        }
        
        std::string content = data.substr(contentStart, length);
        
        // 尝试解析JSON
        nlohmann::json jsonResult;
        if (tryParseJson(content, jsonResult)) {
            // 如果是有效的JSON字符串，直接返回原始内容
            // 具体业务逻辑根据需要处理JSON对象
            return content;
        }
        
        return content;
    }
    
    // 其他类型的响应处理可以在此添加
    
    return "";
}

bool RedisProtocol::tryParseJson(const std::string& content, nlohmann::json& result) {
    if (content.empty()) {
        return false;
    }
    
    // 检查是否可能是JSON格式
    if ((content[0] != '{' && content[0] != '[') || 
        (content[0] == '{' && content.back() != '}') ||
        (content[0] == '[' && content.back() != ']')) {
        return false;
    }
    
    try {
    result = nlohmann::json::parse(content);
        return true;
    } catch (const std::exception&) {
        // 使用标准异常类代替特定的json库异常类
        return false;
    }
}

std::vector<std::string> RedisProtocol::parseCommand(const std::string& command) {
    std::vector<std::string> args;
    
    if (command.empty() || command[0] != '*') {
        return args;
    }
    
    size_t pos = 1;
    size_t endPos = command.find("\r\n", pos);
    if (endPos == std::string::npos) {
        return args;
    }
    
    int argCount;
    try {
        argCount = std::stoi(command.substr(pos, endPos - pos));
    } catch (const std::exception&) {
        return args;
    }
    
    pos = endPos + 2;  // 跳过 \r\n
    
    for (int i = 0; i < argCount; ++i) {
        // 每个参数都应该以 $ 开头
        if (pos >= command.length() || command[pos] != '$') {
            break;
        }
        
        // 获取参数长度
        endPos = command.find("\r\n", pos + 1);
        if (endPos == std::string::npos) {
            break;
        }
        
        int argLength;
        try {
            argLength = std::stoi(command.substr(pos + 1, endPos - pos - 1));
        } catch (const std::exception&) {
            break;
        }
        
        pos = endPos + 2;  // 跳过 \r\n
        
        // 获取参数内容
        if (pos + argLength > command.length()) {
            break;
        }
        
        args.push_back(command.substr(pos, argLength));
        pos += argLength + 2;  // 跳过参数内容和 \r\n
    }
    
    return args;
}

std::string RedisProtocol::encodeGetResponse(const std::string& value) {
    if (value.empty()) {
        // 返回nil
        return "*1\r\n$3\r\nnil\r\n";
    }
    
    // 尝试解析为JSON
    nlohmann::json jsonValue;
    if (tryParseJson(value, jsonValue)) {
        // 如果是JSON格式，使用原有的JSON编码
        return encodeJson(jsonValue);
    }
    
    // 将值按空格拆分
    std::vector<std::string> parts;
    std::string part;
    std::istringstream iss(value);
    while (iss >> part) {
        parts.push_back(part);
    }
    
    // 返回数组格式
    return encodeArray(parts);
}

} // namespace raft 