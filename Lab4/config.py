"""
配置文件 - 所有系统配置参数集中管理
"""
import os
from cryptography.fernet import Fernet

# 密钥存储路径
FERNET_KEY_FILE = "fernet_key.txt"

def get_or_create_fernet_key():
    """获取或创建Fernet密钥"""
    if os.path.exists(FERNET_KEY_FILE):
        # 确保现有文件权限安全
        os.chmod(FERNET_KEY_FILE, 0o600)  # 仅所有者可读写
        with open(FERNET_KEY_FILE, "rb") as f:
            return f.read()
    else:
        key = Fernet.generate_key()
        # 使用更安全的创建方式
        fd = os.open(FERNET_KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as f:
            f.write(key)
        return key

# 全局加密密钥
FERNET_KEY = get_or_create_fernet_key()

# 服务器配置
HOST = "0.0.0.0"
PORT = 5000
DEBUG = True

# API配置（示例值，用户需要在界面中添加实际后端）
API_BASE_URL = os.environ.get("API_BASE_URL", "https://api.openai.com/v1")
DEFAULT_MODEL = os.environ.get("MODEL_NAME", "gpt-3.5-turbo")
DEFAULT_API_KEY = os.environ.get("API_KEY", "")  # 用户需要提供自己的API密钥

# 后端配置
DEFAULT_BACKENDS = []  # 不预设任何后端，需要用户手动添加

# Raft数据库配置
RAFT_HOST = os.environ.get("RAFT_HOST", "127.0.0.1")
RAFT_PORTS = [8001, 8002, 8003]  # 默认的Raft节点端口
RAFT_TIMEOUT = 3  # 连接超时秒数

# 聊天历史配置
MAX_HISTORY_CHARS = 100000  # 聊天历史最大字符数(实际为Lab3限制的32k)

# 日志配置
LOG_FILE = "log.txt"
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")  # DEBUG, INFO, WARNING, ERROR 
