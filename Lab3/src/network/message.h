#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "../include/constants.h"

namespace raft {

// 消息类型枚举
enum class MessageType : uint32_t {
    REQUESTVOTE_REQUEST = 1,
    REQUESTVOTE_RESPONSE = 2,
    APPENDENTRIES_REQUEST = 3,
    APPENDENTRIES_RESPONSE = 4
};

// 日志条目结构
struct LogEntry {
    int term;                // 条目的任期
    std::string data;        // 条目的数据
    
    // 序列化方法
    std::string serialize() const;
    static LogEntry deserialize(const char* data, size_t size);
    static LogEntry deserialize(const std::string& data);
    size_t getSerializedSize() const;
};

// 消息头结构
struct MessageHeader {
    MessageType type;         // 消息类型
    uint32_t payload_size;    // 负载大小（不包括消息头）
};

// 基础消息接口
class Message {
public:
    virtual ~Message() {}
    
    // 序列化为字符串
    virtual std::string serialize() const = 0;
    
    // 反序列化
    virtual bool deserialize(const char* data, size_t size) = 0;
    virtual bool deserialize(const std::string& data) {
        return deserialize(data.c_str(), data.size());
    }
    
    // 获取消息类型
    virtual MessageType getType() const = 0;
    
    // 创建完整的网络消息（包括头和序列化后的负载）
    std::string createNetworkMessage() const {
        std::string payload = serialize();
        MessageHeader header{getType(), static_cast<uint32_t>(payload.size())};
        
        std::string result;
        result.resize(sizeof(MessageHeader) + payload.size());
        std::memcpy(&result[0], &header, sizeof(MessageHeader));
        std::memcpy(&result[sizeof(MessageHeader)], payload.c_str(), payload.size());
        
        return result;
    }
    
    // 从网络消息中提取消息体
    static std::pair<MessageType, std::string> extractPayload(const char* data, size_t size) {
        if (size < sizeof(MessageHeader)) {
            throw std::runtime_error("消息太短，无法提取头");
        }
        
        const MessageHeader* header = reinterpret_cast<const MessageHeader*>(data);
        if (size < sizeof(MessageHeader) + header->payload_size) {
            throw std::runtime_error("消息不完整");
        }
        
        std::string payload(data + sizeof(MessageHeader), header->payload_size);
        return {header->type, payload};
    }
};

// 请求投票消息
class RequestVoteRequest : public Message {
public:
    int term;               // 候选者的任期
    int candidate_id;       // 候选者ID
    int last_log_index;     // 候选者的最后日志索引
    int last_log_term;      // 候选者最后日志的任期
    
    MessageType getType() const override {
        return MessageType::REQUESTVOTE_REQUEST;
    }
    
    std::string serialize() const override;
    bool deserialize(const char* data, size_t size) override;
};

// 投票响应消息
class RequestVoteResponse : public Message {
public:
    int term;               // 当前任期号
    bool vote_granted;      // 是否投票给候选者
    
    MessageType getType() const override {
        return MessageType::REQUESTVOTE_RESPONSE;
    }
    
    std::string serialize() const override;
    bool deserialize(const char* data, size_t size) override;
};

// 附加日志请求消息
class AppendEntriesRequest : public Message {
public:
    int term;                           // 领导者的任期
    int leader_id;                      // 领导者ID
    int prev_log_index;                 // 前一个日志的索引
    int prev_log_term;                  // 前一个日志的任期
    int leader_commit;                  // 领导者的提交索引
    std::vector<LogEntry> entries;      // 要附加的日志条目
    int seq;                            // 序列号，用于标识请求

    MessageType getType() const override {
        return MessageType::APPENDENTRIES_REQUEST;
    }
    
    std::string serialize() const override;
    bool deserialize(const char* data, size_t size) override;
};

// 附加日志响应消息
class AppendEntriesResponse : public Message {
public:
    int term;               // 当前任期号
    int follower_id;        // 跟随者ID
    int log_index;          // 跟随者的最新日志索引
    bool success;           // 是否成功添加日志
    int follower_commit;    // 跟随者的提交索引
    int ack;                // 确认的序列号

    MessageType getType() const override {
        return MessageType::APPENDENTRIES_RESPONSE;
    }
    
    std::string serialize() const override;
    bool deserialize(const char* data, size_t size) override;
};


// 根据消息类型创建具体消息对象
std::unique_ptr<Message> createMessage(MessageType type);

// 从完整的网络消息中解析出具体的消息对象
std::unique_ptr<Message> parseMessage(const char* data, size_t size);
std::unique_ptr<Message> parseMessage(const std::string& data);

} // namespace raft

#endif // MESSAGE_H 