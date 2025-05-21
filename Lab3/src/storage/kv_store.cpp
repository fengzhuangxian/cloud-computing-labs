#include "kv_store.h"

namespace raft {

std::string KVStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return "";
    }
    return it->second;
}

void KVStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);
    store_[key] = value;
}

void KVStore::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    store_.erase(key);
}

void KVStore::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    store_.clear();
}

} // namespace raft 