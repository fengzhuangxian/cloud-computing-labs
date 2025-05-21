#include "message.h"
#include <cstring>
#include <stdexcept>

namespace raft {

// ---------- LogEntry 实现 ----------
std::string LogEntry::serialize() const {
    // 格式: [term(4字节)][数据长度(4字节)][数据内容]
    size_t total_size = sizeof(int) + sizeof(uint32_t) + data.size();
    std::string result;
    result.resize(total_size);
    
    char* ptr = &result[0];
    
    // 写入term
    std::memcpy(ptr, &term, sizeof(int));
    ptr += sizeof(int);
    
    // 写入数据长度
    uint32_t data_size = static_cast<uint32_t>(data.size());
    std::memcpy(ptr, &data_size, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // 写入数据内容
    std::memcpy(ptr, data.c_str(), data.size());
    
    return result;
}

LogEntry LogEntry::deserialize(const char* data, size_t size) {
    if (size < sizeof(int) + sizeof(uint32_t)) {
        throw std::runtime_error("LogEntry反序列化错误: 数据太短");
    }
    
    LogEntry entry;
    const char* ptr = data;
    
    // 读取term
    std::memcpy(&entry.term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    // 读取数据长度
    uint32_t data_size;
    std::memcpy(&data_size, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // 验证数据长度
    if (size < sizeof(int) + sizeof(uint32_t) + data_size) {
        throw std::runtime_error("LogEntry反序列化错误: 数据不完整");
    }
    
    // 读取数据内容
    entry.data.assign(ptr, data_size);
    
    return entry;
}

LogEntry LogEntry::deserialize(const std::string& data) {
    return deserialize(data.c_str(), data.size());
}

size_t LogEntry::getSerializedSize() const {
    return sizeof(int) + sizeof(uint32_t) + data.size();
}

// ---------- RequestVoteRequest 实现 ----------
std::string RequestVoteRequest::serialize() const {
    // 格式: [term(4字节)][candidate_id(4字节)][last_log_index(4字节)][last_log_term(4字节)]
    std::string result;
    result.resize(4 * sizeof(int));
    
    char* ptr = &result[0];
    
    std::memcpy(ptr, &term, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &candidate_id, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &last_log_index, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &last_log_term, sizeof(int));
    
    return result;
}

bool RequestVoteRequest::deserialize(const char* data, size_t size) {
    if (size < 4 * sizeof(int)) {
        return false;
    }
    
    const char* ptr = data;
    
    std::memcpy(&term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&candidate_id, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&last_log_index, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&last_log_term, ptr, sizeof(int));
    
    return true;
}

// ---------- RequestVoteResponse 实现 ----------
std::string RequestVoteResponse::serialize() const {
    // 格式: [term(4字节)][vote_granted(1字节)]
    std::string result;
    result.resize(sizeof(int) + sizeof(bool));
    
    char* ptr = &result[0];
    
    std::memcpy(ptr, &term, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &vote_granted, sizeof(bool));
    
    return result;
}

bool RequestVoteResponse::deserialize(const char* data, size_t size) {
    if (size < sizeof(int) + sizeof(bool)) {
        return false;
    }
    
    const char* ptr = data;
    
    std::memcpy(&term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&vote_granted, ptr, sizeof(bool));
    
    return true;
}

// ---------- AppendEntriesRequest 实现 ----------
std::string AppendEntriesRequest::serialize() const {
    // 计算总大小: 基本字段 + 条目计数 + 所有条目的序列化大小
    size_t entries_size = 0;
    for (const auto& entry : entries) {
        entries_size += entry.getSerializedSize();
    }
    
    // 格式: [term(4)][leader_id(4)][prev_log_index(4)][prev_log_term(4)][leader_commit(4)][seq(4)][条目数(4)][条目...]
    size_t total_size = 6 * sizeof(int) + sizeof(uint32_t) + entries_size;
    std::string result;
    result.resize(total_size);
    
    char* ptr = &result[0];
    
    std::memcpy(ptr, &term, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &leader_id, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &prev_log_index, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &prev_log_term, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &leader_commit, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &seq, sizeof(int));
    ptr += sizeof(int);
    
    // 写入条目数
    uint32_t entry_count = static_cast<uint32_t>(entries.size());
    std::memcpy(ptr, &entry_count, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // 写入每个条目
    for (const auto& entry : entries) {
        std::string entry_data = entry.serialize();
        std::memcpy(ptr, entry_data.c_str(), entry_data.size());
        ptr += entry_data.size();
    }
    
    return result;
}

bool AppendEntriesRequest::deserialize(const char* data, size_t size) {
    if (size < 6 * sizeof(int) + sizeof(uint32_t)) {
        return false;
    }
    
    const char* ptr = data;
    
    std::memcpy(&term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&leader_id, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&prev_log_index, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&prev_log_term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&leader_commit, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&seq, ptr, sizeof(int));
    ptr += sizeof(int);
    
    // 读取条目数
    uint32_t entry_count;
    std::memcpy(&entry_count, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // 清空原有条目
    entries.clear();
    
    // 读取每个条目
    try {
        size_t remaining_size = size - (ptr - data);
        for (uint32_t i = 0; i < entry_count && remaining_size > 0; ++i) {
            // 尝试反序列化一个条目
            LogEntry entry = LogEntry::deserialize(ptr, remaining_size);
            
            // 添加到条目列表
            entries.push_back(entry);
            
            // 移动指针
            size_t entry_size = entry.getSerializedSize();
            ptr += entry_size;
            remaining_size -= entry_size;
        }
        
        // 检查是否所有条目都成功反序列化
        if (entries.size() != entry_count) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    
    return true;
}

// ---------- AppendEntriesResponse 实现 ----------
std::string AppendEntriesResponse::serialize() const {
    // 格式: [term(4)][follower_id(4)][log_index(4)][success(1)][follower_commit(4)][ack(4)]
    std::string result;
    result.resize(5 * sizeof(int) + sizeof(bool));
    
    char* ptr = &result[0];
    
    std::memcpy(ptr, &term, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &follower_id, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &log_index, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &success, sizeof(bool));
    ptr += sizeof(bool);
    
    std::memcpy(ptr, &follower_commit, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(ptr, &ack, sizeof(int));
    
    return result;
}

bool AppendEntriesResponse::deserialize(const char* data, size_t size) {
    if (size < 5 * sizeof(int) + sizeof(bool)) {
        return false;
    }
    
    const char* ptr = data;
    
    std::memcpy(&term, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&follower_id, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&log_index, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&success, ptr, sizeof(bool));
    ptr += sizeof(bool);
    
    std::memcpy(&follower_commit, ptr, sizeof(int));
    ptr += sizeof(int);
    
    std::memcpy(&ack, ptr, sizeof(int));
    
    return true;
}


// ---------- 工厂方法实现 ----------
std::unique_ptr<Message> createMessage(MessageType type) {
    switch (type) {
        case MessageType::REQUESTVOTE_REQUEST:
            return std::make_unique<RequestVoteRequest>();
        case MessageType::REQUESTVOTE_RESPONSE:
            return std::make_unique<RequestVoteResponse>();
        case MessageType::APPENDENTRIES_REQUEST:
            return std::make_unique<AppendEntriesRequest>();
        case MessageType::APPENDENTRIES_RESPONSE:
            return std::make_unique<AppendEntriesResponse>();
        default:
            throw std::runtime_error("未知的消息类型");
    }
}

std::unique_ptr<Message> parseMessage(const char* data, size_t size) {
    try {
        // 替换结构化绑定语法
        std::pair<MessageType, std::string> extractResult = Message::extractPayload(data, size);
        MessageType type = extractResult.first;
        std::string payload = extractResult.second;
        
        auto message = createMessage(type);
        if (!message->deserialize(payload)) {
            throw std::runtime_error("消息反序列化失败");
        }
        return message;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("消息解析失败: ") + e.what());
    }
}

std::unique_ptr<Message> parseMessage(const std::string& data) {
    return parseMessage(data.c_str(), data.size());
}

} // namespace raft 