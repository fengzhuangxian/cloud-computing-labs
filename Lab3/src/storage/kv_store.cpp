#include "kv_store.h"
#include <stdexcept>

namespace raft
{

    std::string KVStore::get(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = store_.find(key);
        if (it == store_.end())
        {
            return "";
        }
        return it->second;
    }

    void KVStore::set(const std::string &key, const std::string &value)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        store_[key] = value;
    }

    void KVStore::del(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        store_.erase(key);
    }

    void KVStore::clear()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        store_.clear();
    }

    // 保存快照到文件（二进制格式）
    void KVStore::save_snapshot(const std::string &snapshot_file)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ofstream out(snapshot_file, std::ios::binary);
        if (!out)
        {
            throw std::runtime_error("Failed to open snapshot file: " + snapshot_file);
        }

        for (const auto &[key, value] : store_)
        {
            // 检查键和值是否包含换行符
            if (key.find('\n') != std::string::npos || value.find('\n') != std::string::npos)
            {
                throw std::runtime_error("Key or value contains newline character");
            }

            // 写入键长度 + 键内容
            uint32_t key_size = key.size();
            out.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
            out.write(key.data(), key_size);

            // 写入值长度 + 值内容
            uint32_t value_size = value.size();
            out.write(reinterpret_cast<const char *>(&value_size), sizeof(value_size));
            out.write(value.data(), value_size);

            // 添加换行符分隔键值对
            out.write("\n", 1);
        }
    }

    bool KVStore::restore_from_snapshot(const std::string &snapshot_file)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ifstream in(snapshot_file, std::ios::binary);
        if (!in)
        {
            return false; // 文件不存在视为正常（首次启动）
        }

        store_.clear();

        while (true)
        {
            uint32_t key_size;
            // 读取键长度
            in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
            if (in.eof())
                break; // 正常结束

            // 读取键内容
            std::string key(key_size, '\0');
            in.read(&key[0], key_size);

            // 读取值长度
            uint32_t value_size;
            in.read(reinterpret_cast<char *>(&value_size), sizeof(value_size));

            // 读取值内容
            std::string value(value_size, '\0');
            in.read(&value[0], value_size);

            // 检查键和值是否包含换行符
            if (key.find('\n') != std::string::npos || value.find('\n') != std::string::npos)
            {
                throw std::runtime_error("Corrupted snapshot: key or value contains newline");
            }

            store_.emplace(std::move(key), std::move(value));

            // 读取并验证换行符
            char newline;
            in.read(&newline, 1);
            if (in.eof())
            {
                // 文件结束是正常的（最后一个键值对）
                break;
            }
            if (newline != '\n')
            {
                throw std::runtime_error("Invalid snapshot format: expected newline");
            }
        }

        return true;
    }

} // namespace raft