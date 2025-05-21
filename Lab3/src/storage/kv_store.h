#ifndef KV_STORE_H
#define KV_STORE_H

#include <string>
#include <unordered_map>
#include <mutex>

namespace raft {

// KV存储类，作为状态机
class KVStore {
public:
    KVStore() = default;
    ~KVStore() = default;

    // 获取键的值
    std::string get(const std::string& key);

    // 设置键值
    void set(const std::string& key, const std::string& value);

    // 删除键
    void del(const std::string& key);

    // 清空所有存储
    void clear();

private:
    // 存储的键值对
    std::unordered_map<std::string, std::string> store_;
    
    // 保护存储操作的互斥锁
    std::mutex mtx_;
};

} // namespace raft

#endif // KV_STORE_H 