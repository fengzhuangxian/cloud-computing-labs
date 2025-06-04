"""
聊天API模块 - 提供聊天相关的API端点
"""
from flask import Blueprint, request, jsonify
import traceback
import time
import threading
from utils.logger import log_info
from modules.chat_history import get_chat_history, save_chat_history
from modules.llm_client import create_client
from modules.backend_manager import get_available_backend
from config import DEFAULT_MODEL

# 创建蓝图
chat_api = Blueprint('chat_api', __name__)

# 请求超时时间（秒）
REQUEST_TIMEOUT = 120  # 2分钟

def get_client_id():
    """
    获取客户端唯一标识
    
    返回:
        str: 客户端IP或其他标识
    """
    client_ip = request.remote_addr
    print(client_ip)
    if request.environ.get('HTTP_X_FORWARDED_FOR'):
        client_ip = request.environ['HTTP_X_FORWARDED_FOR']
    
    # 简单的哈希处理，避免直接使用IP
    # client_id = f"client_{hash(client_ip) % 10000:04d}"
    client_id = f"client_{client_ip}"  # 使用IP地址作为标识
    
    return client_id

class TimeoutError(Exception):
    """请求超时异常"""
    pass

def call_with_timeout(func, timeout, *args, **kwargs):
    """
    使用超时执行函数
    
    参数:
        func: 要执行的函数
        timeout: 超时时间（秒）
        args, kwargs: 传递给函数的参数
        
    返回:
        函数的返回值
        
    异常:
        TimeoutError: 如果函数执行超时
    """
    result = [None]
    exception = [None]
    completed = [False]
    
    def worker():
        try:
            result[0] = func(*args, **kwargs)
            completed[0] = True
        except Exception as e:
            exception[0] = e
    
    thread = threading.Thread(target=worker)
    thread.daemon = True
    thread.start()
    
    thread.join(timeout)
    
    if not completed[0]:
        if exception[0]:
            raise exception[0]
        raise TimeoutError(f"操作超时（{timeout}秒）")
    
    if exception[0]:
        raise exception[0]
        
    return result[0]

@chat_api.route("/chat", methods=["POST"])
def chat():
    """
    处理聊天请求
    
    请求体:
        message (str): 用户消息
        
    返回:
        JSON: 包含模型回复的JSON对象
    """
    try:
        # 获取请求数据
        data = request.get_json()
        user_message = data.get('message', '')
        if not user_message.strip():
            return jsonify({"error": "消息不能为空"}), 400
        
        # 获取客户端ID
        client_id = get_client_id()
        
        # 获取历史对话
        messages = get_chat_history(client_id)
        print("history message",messages)
        # 添加用户消息
        messages.append({"role": "user", "content": user_message})
        
        # 记录用户请求
        log_info("用户发送消息", {"client_id": client_id, "message": user_message})
        
        # 获取可用后端
        backend = get_available_backend()
        if not backend:
            return jsonify({"error": "没有可用的语言模型后端", "no_backend": True}), 503
        
        # 获取后端ID
        backend_id = backend.get("backend_id")
        
        # 创建LLM客户端
        llm_client = create_client(backend)
        
        try:
            # 使用超时调用LLM API
            start_time = time.time()
            model_name = backend.get("model", DEFAULT_MODEL)
            
            # 带超时的模型调用
            assistant_response = call_with_timeout(
                llm_client.chat, 
                REQUEST_TIMEOUT, 
                messages, 
                model=model_name
            )
            
            # 记录请求耗时
            elapsed_time = time.time() - start_time
            log_info("模型响应完成", {"client_id": client_id, "耗时": f"{elapsed_time:.2f}秒"})
            #print(assistant_response)
            # 添加助手回复到历史记录
            messages.append({"role": "assistant", "content": assistant_response})
            
            # 保存更新后的历史记录
            save_chat_history(client_id, messages)
            
            # 返回响应
            return jsonify({
                "message": {
                    "role": "assistant",
                    "content": assistant_response
                },
                "model": model_name,
                "backend_id": backend_id,
                "reply": assistant_response  # 兼容旧版前端
            })
            
        except TimeoutError:
            # 请求超时处理
            timeout_message = f"请求处理超时（超过{REQUEST_TIMEOUT}秒）。请尝试简化您的问题或稍后再试。"
            log_info("请求超时", {"client_id": client_id, "backend_id": backend_id})
            
            # 添加超时消息到历史记录
            messages.append({"role": "system", "content": timeout_message})
            save_chat_history(client_id, messages)
            
            return jsonify({
                "error": timeout_message,
                "timeout": True,
                "backend_id": backend_id
            }), 504  # Gateway Timeout
        
            #这里可以添加更新后端状态的代码


            
            
    except Exception as e:
        error_details = traceback.format_exc()
        log_info("聊天请求处理错误", {"error": str(e), "details": error_details})
        return jsonify({"error": str(e)}), 500


@chat_api.route("/history", methods=["GET"])
def chat_history():
    """
    获取聊天历史记录
    
    返回:
        JSON: 包含历史消息的JSON对象
    """
    try:
        client_id = get_client_id()
        messages = get_chat_history(client_id)
        
        return jsonify({"messages": messages})
    
    except Exception as e:
        error_details = traceback.format_exc()
        log_info("获取聊天历史错误", {"error": str(e), "details": error_details})
        return jsonify({"error": str(e)}), 500
    

@chat_api.route("/history", methods=["DELETE"])
def clear_chat_history():
    """
    清除聊天历史记录
    
    返回:
        JSON: 操作结果
    """
    try:
        client_id = get_client_id()
        save_chat_history(client_id, [])  # 清空历史记录
        
        log_info("清除聊天历史", {"client_id": client_id})
        return jsonify({"message": "聊天历史已清除"})
    
    except Exception as e:
        error_details = traceback.format_exc()
        log_info("清除聊天历史错误", {"error": str(e), "details": error_details})
        return jsonify({"error": str(e)}), 500