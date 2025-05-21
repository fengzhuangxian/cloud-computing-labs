#ifndef REDIS_PROTOCOL_H
#define REDIS_PROTOCOL_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace raft {

/**
 * Redis协议(RESP)编解码工具类
 */
class RedisProtocol {
public:
    /**
     * 编码值为RESP格式
     * @param value 要编码的字符串
     * @return RESP格式的字符串
     */
    static std::string encode(const std::string& value);
    
    /**
     * 编码空值为RESP格式
     * @return RESP格式的空值字符串 ($-1\r\n)
     */
    static std::string encodeNull();
    
    /**
     * 编码JSON为RESP格式
     * @param json JSON对象
     * @return RESP格式的字符串
     */
    static std::string encodeJson(const nlohmann::json& json);
    
    /**
     * 编码错误信息为RESP格式
     * @param error 错误信息
     * @return RESP格式的错误字符串
     */
    static std::string encodeError(const std::string& error);
    
    /**
     * 编码状态为RESP格式
     * @param status 状态信息
     * @return RESP格式的状态字符串
     */
    static std::string encodeStatus(const std::string& status);
    
    /**
     * 编码整数为RESP格式
     * @param value 整数值
     * @return RESP格式的整数字符串
     */
    static std::string encodeInteger(int value);
    
    /**
     * 编码数组为RESP格式
     * @param items 字符串数组
     * @return RESP格式的数组字符串
     */
    static std::string encodeArray(const std::vector<std::string>& items);
    
    /**
     * 专门处理GET命令的响应，将值按空格拆分为数组格式
     * @param value 要编码的字符串
     * @return RESP格式的数组字符串
     */
    static std::string encodeGetResponse(const std::string& value);
    
    /**
     * 解码RESP格式的数据
     * @param data RESP格式的数据
     * @return 解码后的字符串，如果是JSON会自动解析
     */
    static std::string decode(const std::string& data);
    
    /**
     * 尝试解析JSON字符串
     * @param content 可能的JSON字符串
     * @return 如果是有效JSON，返回true并将结果存入result，否则返回false
     */
    static bool tryParseJson(const std::string& content, nlohmann::json& result);
    
    /**
     * 解析RESP格式的命令为参数数组
     * @param command RESP格式的命令
     * @return 解析后的命令参数列表
     */
    static std::vector<std::string> parseCommand(const std::string& command);
};

} // namespace raft

#endif // REDIS_PROTOCOL_H 