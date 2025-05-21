"""
日志模块 - 提供应用程序日志记录功能，专注于上下文操作记录
"""
import time
import json
import os
from config import LOG_FILE

def log_info(message, details=None):
    """
    记录INFO级别日志
    
    参数:
        message (str): 日志消息内容
        details (dict): 额外的日志详情，如上下文内容
    """
    # 获取当前时间
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    
    # 确保details是字典
    if details is None:
        details = {}
    elif not isinstance(details, dict):
        details = {"value": str(details)}
    
    # 构建日志条目
    log_entry = {
        "timestamp": timestamp,
        "message": message,
        "details": details
    }
    
    # 转为JSON格式
    log_json = json.dumps(log_entry, ensure_ascii=False)
    
    # 写入日志文件
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(log_json + "\n")
    except Exception as e:
        print(f"写入日志失败: {e}")
        print(log_json)  # 输出到控制台作为备用

def log_error(message, details=None):
    """
    记录ERROR级别日志
    
    参数:
        message (str): 错误消息内容
        details (dict): 额外的错误详情，如上下文内容
    """
    # 获取当前时间
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    
    # 确保details是字典
    if details is None:
        details = {}
    elif not isinstance(details, dict):
        details = {"value": str(details)}
    
    # 构建日志条目
    log_entry = {
        "timestamp": timestamp,
        "level": "ERROR",
        "message": message,
        "details": details
    }
    
    # 转为JSON格式
    log_json = json.dumps(log_entry, ensure_ascii=False)
    
    # 写入日志文件
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(log_json + "\n")
    except Exception as e:
        print(f"写入错误日志失败: {e}")
        print(log_json)  # 输出到控制台作为备用

# 初始化函数，在应用启动时清空日志文件
def init_logger():
    """初始化日志系统，清空旧的日志文件"""
    try:
        # 确保日志目录存在
        log_dir = os.path.dirname(LOG_FILE)
        if log_dir and not os.path.exists(log_dir):
            os.makedirs(log_dir)
            
        # 清空或创建日志文件
        open(LOG_FILE, "w", encoding="utf-8").close()
        
        # 记录初始化日志
        log_info("日志系统初始化完成", {"time": time.strftime("%Y-%m-%d %H:%M:%S")})
        return True
    except Exception as e:
        print(f"初始化日志系统失败: {e}")
        return False 