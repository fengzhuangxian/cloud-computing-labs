"""
应用初始化模块 - 负责应用启动时的初始化工作
"""
from utils.logger import log_info
from modules.backend_manager import initialize_backends

def initialize_app():
    """
    初始化应用
    
    此函数应该在应用启动时调用，完成所有必要的初始化工作
    """
    log_info("初始化应用")
    
    # 初始化后端管理器
    initialize_backends()
    
    log_info("应用初始化完成") 