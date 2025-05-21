"""
主应用文件 - 负载均衡聊天系统的入口点
"""
from flask import Flask, render_template
from api.chat_api import chat_api
from api.backend_api import backend_api
from utils.init import initialize_app
from utils.logger import log_info, init_logger
from config import HOST, PORT, DEBUG

# 创建Flask应用
app = Flask(__name__)

# 注册蓝图
app.register_blueprint(chat_api, url_prefix='')
app.register_blueprint(backend_api, url_prefix='/api')

# 主页路由
@app.route("/")
def chat():
    return render_template("chat.html")


if __name__ == "__main__":
    # 初始化日志系统
    init_logger()
    
    # 初始化应用
    initialize_app()
    
    # 启动Web服务器
    log_info("启动Web服务器", {"host": HOST, "port": PORT})
    app.run(debug=DEBUG, host=HOST, port=PORT) 