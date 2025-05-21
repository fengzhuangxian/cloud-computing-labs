"""
聊天历史管理模块 - 负责存储和检索用户聊天历史
"""
import json
from modules.resp_handler import resp_decode, raft_set, raft_get
from utils.logger import log_info
from config import MAX_HISTORY_CHARS

def get_chat_history(client_id):
    """
    获取指定客户端的聊天历史
    
    参数:
        client_id (str): 客户端标识
        
    返回:
        list: 聊天消息列表
    """
    try:
        # 从Raft KV存储中获取历史记录
        resp = raft_get(client_id)
        print("resp:",resp)
        # 使用RESP解码器
        history_data = resp_decode(resp)
        
        # 记录操作
        log_info("获取聊天历史", {"client_id": client_id})
        
        # 检查解码结果
        if history_data is None:
            print("history is null")
            return []
            
        # 如果已经是列表，直接返回
        if isinstance(history_data, list):
            return history_data
            
        # 尝试解析为JSON
        try:
            if isinstance(history_data, str):
                messages = json.loads(history_data)
                return messages
        except json.JSONDecodeError:
            pass
        
        return []
    except Exception as e:
        log_info("获取聊天历史失败", {"client_id": client_id, "error": str(e)})
        print("获取聊天历史失败")
        return []

def save_chat_history(client_id, messages):
    """
    保存聊天历史到Raft KV存储
    
    参数:
        client_id (str): 客户端标识
        messages (list): 消息列表
        
    返回:
        str: 保存结果
    """
    try:
        # 智能截断确保不超过大小限制
        truncated_messages = smart_truncate_messages(messages)
        
        # 转为JSON字符串
        messages_json = json.dumps(truncated_messages)
        
        # 记录操作
        log_info("保存聊天历史", {"client_id": client_id, "消息数量": len(truncated_messages)})
        
        # 保存到Raft KV存储
        result = raft_set(client_id, messages_json)
        
        # 检查返回结果
        if result and (result.startswith("+OK") or "*3\r\n$3\r\nSET\r\n" in result):
            return "+OK"
        
        return result
    except Exception as e:
        log_info("保存聊天历史失败", {"client_id": client_id, "error": str(e)})
        return f"$-1\r\nERROR:{str(e)}\r\n"

def smart_truncate_messages(messages):
    """
    智能截断消息历史，确保不超过大小限制且保留完整对话格式
    
    策略：
    1. 保留最新的对话
    2. 确保用户问题和AI回答成对保留
    3. 添加系统提示表明历史被截断
    
    参数:
        messages (list): 原始消息列表
        
    返回:
        list: 截断后的消息列表
    """
    if not messages:
        return []
    
    # 编码为JSON检查大小
    full_json = json.dumps(messages)
    
    # 检查是否已经在限制内
    if len(full_json) <= MAX_HISTORY_CHARS:
        print("not out len!------------------------")
        return messages
    print("----------------------out len!")
    # 保留最新的几轮对话
    kept_messages = []
    current_size = 0
    max_size = MAX_HISTORY_CHARS - 200  # 预留空间给系统消息和格式化
    
    # 从最新消息开始添加，确保对话成对保留
    i = len(messages) - 1
    while i >= 0 and current_size < max_size:
        msg = messages[i]
        msg_json = json.dumps(msg)
        msg_size = len(msg_json) + 2  # +2 for comma and formatting
        
        # 检查是否还有空间添加这条消息
        if current_size + msg_size <= max_size:
            kept_messages.insert(0, msg)
            current_size += msg_size
        else:
            # 如果空间不够，且是对话的一部分，跳过整个对话
            if msg["role"] == "assistant" and i > 0 and messages[i-1]["role"] == "user":
                # 跳过这个对话轮次
                i -= 1
            break
        i -= 1
    
    # 如果没有保留任何消息（所有消息都太大），至少保留最后一轮对话
    if not kept_messages and len(messages) >= 2:
        # 尝试保留最后一轮完整对话（用户问题+AI回答）
        if messages[-2]["role"] == "user" and messages[-1]["role"] == "assistant":
            user_msg = messages[-2]
            ai_msg = messages[-1]
            
            # 计算保留这两条消息需要的空间
            pair_json = json.dumps([user_msg, ai_msg])
            if len(pair_json) > max_size:
                # 如果还是太大，截断内容
                user_content = user_msg["content"]
                ai_content = ai_msg["content"]
                
                # 分配空间，保留更多AI回答
                user_max_len = int(max_size * 0.3)  # 分配30%给用户问题
                ai_max_len = max_size - user_max_len - 100  # 预留一些空间给JSON结构
                
                truncated_user = user_msg.copy()
                truncated_ai = ai_msg.copy()
                
                if len(user_content) > user_max_len:
                    truncated_user["content"] = user_content[:user_max_len] + "..."
                
                if len(ai_content) > ai_max_len:
                    truncated_ai["content"] = ai_content[:ai_max_len] + "..."
                
                kept_messages = [truncated_user, truncated_ai]
            else:
                kept_messages = [user_msg, ai_msg]
    
    # 如果保留的消息数量少于原始消息，添加提示
    if len(kept_messages) < len(messages):
        # 在开头添加系统提示
        system_msg = {
            "role": "system", 
            "content": "由于上下文长度限制，部分历史对话已被截断。"
        }
        
        # 检查是否有足够空间添加系统消息
        system_json = json.dumps(system_msg)
        if MAX_HISTORY_CHARS - current_size >= len(system_json) + 2:
            kept_messages.insert(0, system_msg)
    
    return kept_messages 