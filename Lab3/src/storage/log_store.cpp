#include "log_store.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace raft {

InMemoryLogStore::InMemoryLogStore(const std::string& filename) 
    : file_name_(filename), committed_idx_(0) {
    // 初始化日志，插入一个空白条目作为索引0
    entries_.push_back("");
    terms_.push_back(0);
}

InMemoryLogStore::~InMemoryLogStore() {
    // 确保最后的日志被写入文件
    write_to_file();
}

void InMemoryLogStore::append(const std::string& entry, int term) {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back(entry);
    terms_.push_back(term);
    write_to_file();
}

int InMemoryLogStore::latest_index() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<int>(entries_.size()) - 1;
}

int InMemoryLogStore::latest_term() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (entries_.size() <= 1) {
        return 0;
    }
    return terms_.back();
}

std::string InMemoryLogStore::entry_at(int index) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (index <= 0 || index >= static_cast<int>(entries_.size())) {
        return "";
    }
    return entries_[index];
}

int InMemoryLogStore::term_at(int index) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (index < 0 || index >= static_cast<int>(terms_.size())) {
        return 0;
    }
    return terms_[index];
}

void InMemoryLogStore::erase(int start, int end) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (start <= 0 || start > end || end >= static_cast<int>(entries_.size())) {
        return;
    }
    
    // 确保end不超过数组边界
    end = std::min(end, static_cast<int>(entries_.size()) - 1);
    
    // 删除从start到end的日志条目
    entries_.erase(entries_.begin() + start, entries_.begin() + end + 1);
    terms_.erase(terms_.begin() + start, terms_.begin() + end + 1);
    
    // 删除对应的复制计数
    for (int i = start; i <= end; ++i) {
        num_.erase(i);
    }
    
    // 更新已提交索引（如果需要）
    //committed_idx_ = std::min(committed_idx_, start - 1);
    
    write_to_file();
}

void InMemoryLogStore::commit(int index) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (index > committed_idx_ && index < static_cast<int>(entries_.size())) {
        committed_idx_ = index;
    }
}

int InMemoryLogStore::committed_index() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return committed_idx_;
}

void InMemoryLogStore::add_num(int index, int node_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    // 确保索引有效
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return;
    }
    
    // 确保该节点没有被记录过
    auto& nodes = num_[index];
    if (std::find(nodes.begin(), nodes.end(), node_id) == nodes.end()) {
        nodes.push_back(node_id);
    }
}

int InMemoryLogStore::get_num(int index) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = num_.find(index);
    if (it == num_.end()) {
        return 0;
    }
    return static_cast<int>(it->second.size());
}

void InMemoryLogStore::write_to_file() const {
    // 将日志内容写入文件
    // 延时2秒,模拟写入延迟
    //std::this_thread::sleep_for(std::chrono::seconds(2));//大概2秒左右,千万不能干这个事，会阻塞
    std::ofstream outfile(file_name_, std::ios::trunc); // 覆盖写入
    
    if (!outfile.is_open()) {
        std::cerr << "无法打开日志文件: " << file_name_ << std::endl;
        return;
    }
    
    // 写入日志条目和对应的任期
    for (size_t i = 1; i < entries_.size(); ++i) {
        outfile << "index: " << i << "\tterm: " << terms_[i]  << std::endl;
        outfile << "entry: " << entries_[i] << std::endl;
        outfile << "-------------------------------------" << std::endl;
    }
    
    outfile.close();
}

} // namespace raft 