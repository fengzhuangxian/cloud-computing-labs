"""
LLM客户端模块 - 基于OpenAI兼容API调用各种大模型
"""
import requests
from utils.logger import log_info, log_error

class LLMClient:
    """统一的LLM客户端类，使用OpenAI兼容接口"""
    def __init__(self, config):
        self.api_key = config.get("api_key", "ollama")  # 默认为"ollama"作为占位符
        self.base_url = config.get("base_url", "http://localhost:11434/v1")  # 默认为本地Ollama
        self.model = config.get("model", "llama3")  # 默认模型
        self.timeout = config.get("timeout", 60)  # 超时设置
        
        # 尝试导入OpenAI包
        try:
            from openai import OpenAI
            self.client = OpenAI(
                api_key=self.api_key,
                base_url=self.base_url
            )
            self._use_native_client = True
            log_info("使用OpenAI原生客户端")
        except ImportError:
            log_info("未安装OpenAI包，将使用requests实现兼容")
            self._use_native_client = False
    
    def chat(self, messages, **kwargs):
        """
        发送聊天请求
        
        参数:
            messages (list): 消息历史
            **kwargs: 额外参数
            
        返回:
            str: 模型回复
        """
        try:
            if self._use_native_client:
                return self._native_chat(messages, **kwargs)
            else:
                return self._requests_chat(messages, **kwargs)
        except Exception as e:
            log_error("API调用异常", {"error": str(e)})
            return f"抱歉，API调用出错：{str(e)}"
    
    def _native_chat(self, messages, **kwargs):
        """使用原生OpenAI客户端发送请求"""
        # 准备参数
        params = {
            "model": self.model,
            "messages": messages
        }
        
        # 添加可选参数
        for key in ["temperature", "max_tokens", "top_p", "frequency_penalty", "presence_penalty"]:
            if key in kwargs:
                params[key] = kwargs[key]
        
        # 发送请求
        completion = self.client.chat.completions.create(**params)
        result = completion.choices[0].message.content
        return result
    
    def _requests_chat(self, messages, **kwargs):
        """使用requests模拟OpenAI客户端发送请求"""
        url = f"{self.base_url.rstrip('/')}/chat/completions"
        
        # 准备参数
        data = {
            "model": self.model,
            "messages": messages
        }
        
        # 添加可选参数
        for key in ["temperature", "max_tokens", "top_p", "frequency_penalty", "presence_penalty"]:
            if key in kwargs:
                data[key] = kwargs[key]
        
        # 准备请求头
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.api_key}"
        }
        
        # 发送请求
        response = requests.post(url, headers=headers, json=data, timeout=self.timeout)
        
        # 检查响应状态
        if response.status_code != 200:
            error_msg = f"API调用失败: HTTP {response.status_code}"
            try:
                error_details = response.json()
                error_msg += f" - {error_details.get('error', {}).get('message', '')}"
            except:
                error_msg += f" - {response.text[:100]}"
            
            log_error("API调用失败", {"error": error_msg})
            return f"抱歉，服务器出错了。({error_msg})"
        
        # 处理响应
        result = response.json()
        print(result)
        content = result.get("choices", [{}])[0].get("message", {}).get("content", "")
        return content
    
    def validate(self):
        """验证客户端配置是否有效"""
        if not self.base_url or not self.model:
            return False
        return True

def create_client(backend_config):
    """
    根据后端配置创建客户端
    
    参数:
        backend_config (dict): 后端配置
        
    返回:
        LLMClient: 客户端实例
    """
    return LLMClient(backend_config) 