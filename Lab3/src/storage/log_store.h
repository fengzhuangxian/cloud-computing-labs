#ifndef LOG_STORE_H
#define LOG_STORE_H

#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <fstream>

namespace raft {

// 日志存储接口
class LogStore {
public:
    virtual ~LogStore() = default;

    // 添加日志条目
    virtual void append(const std::string& entry, int term) = 0;
    
    // 获取最新日志索引
    virtual int latest_index() const = 0;
    
    // 获取最新日志的任期
    virtual int latest_term() const = 0;
    
    // 根据索引获取日志条目
    virtual std::string entry_at(int index) const = 0;
    
    // 根据索引获取日志条目的任期
    virtual int term_at(int index) const = 0;
    
    // 删除从start到end的日志条目
    virtual void erase(int start, int end) = 0;
    
    // 提交日志到指定索引
    virtual void commit(int index) = 0;
    
    // 获取已提交的索引位置
    virtual int committed_index() const = 0;

    // 增加某日志条目的复制计数
    virtual void add_num(int index, int node_id) = 0;
    
    // 获取某日志条目的复制计数
    virtual int get_num(int index) const = 0;
};

// 内存实现的日志存储
class InMemoryLogStore : public LogStore {
public:
    InMemoryLogStore(const std::string& filename);
    ~InMemoryLogStore() override;
    
    void append(const std::string& entry, int term) override;
    int latest_index() const override;
    int latest_term() const override;
    std::string entry_at(int index) const override;
    int term_at(int index) const override;
    void erase(int start, int end) override;
    void commit(int index) override;
    int committed_index() const override;
    void add_num(int index, int node_id) override;
    int get_num(int index) const override;
    
private:
    std::string file_name_;                  // 日志文件名
    std::vector<std::string> entries_;       // 日志条目内容
    std::vector<int> terms_;                 // 日志条目的任期
    std::map<int, std::vector<int>> num_;    // 每个日志条目被复制到的节点ID列表
    
    int committed_idx_;                      // 已提交的最大索引
    
    mutable std::mutex mtx_;                 // 保护日志操作的互斥锁
    
    // 将日志内容写入文件
    void write_to_file() const;
};

} // namespace raft

#endif // LOG_STORE_H 