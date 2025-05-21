"""
后端管理API模块 - 提供后端管理相关的API端点
"""
from flask import Blueprint, request, jsonify
from modules.backend_manager import (
    get_backends, 
    get_backend, 
    add_backend, 
    update_backend, 
    delete_backend
)
from utils.logger import log_info

# 创建蓝图
backend_api = Blueprint('backend_api', __name__)

@backend_api.route("/backends", methods=["GET"])
def list_backends():
    """
    获取所有后端列表
    
    返回:
        JSON: 包含所有后端信息的JSON对象
    """
    try:
        backends = get_backends()
        backend_list = []
        
        # 格式化返回数据
        for idx, backend in enumerate(backends):
            backend_info = {
                "id": idx,
                "base_url": backend["base_url"],
                "model": backend.get("model", ""),
                "api_key": backend.get("api_key", ""),
                "weight": backend["weight"],
                "status": backend["status"],
                "failures": backend.get("failures", 0),
                "last_check": backend.get("last_check", 0)
            }
            backend_list.append(backend_info)
            
        return jsonify({"backends": backend_list})
    except Exception as e:
        log_info("获取后端列表失败", {"error": str(e)})
        return jsonify({"error": str(e)}), 500

@backend_api.route("/backends", methods=["POST"])
def create_backend():
    """
    创建新后端
    
    请求体:
        base_url (str): 后端基础URL
        model (str): 模型名称
        api_key (str): API密钥
        weight (int): 权重，默认为1
        
    返回:
        JSON: 操作结果
    """
    try:
        data = request.get_json()
        base_url = data.get("base_url", "").strip()
        model = data.get("model", "").strip()
        api_key = data.get("api_key", "ollama").strip()
        weight = data.get("weight", 1)
        
        if not base_url:
            return jsonify({"error": "基础URL不能为空"}), 400
        
        if not model:
            return jsonify({"error": "模型名称不能为空"}), 400
            
        success, message = add_backend(base_url=base_url, model=model, api_key=api_key, weight=weight)
        
        if success:
            return jsonify({"status": "success", "message": message})
        else:
            return jsonify({"error": message}), 400
    except Exception as e:
        log_info("添加后端失败", {"error": str(e)})
        return jsonify({"error": str(e)}), 500

@backend_api.route("/backends/<backend_id>", methods=["PUT"])
def update_backend_handler(backend_id):
    """
    更新后端信息
    
    参数:
        backend_id (str): 后端ID
        
    请求体:
        base_url (str, optional): 更新的基础URL
        model (str, optional): 更新的模型名称
        api_key (str, optional): 更新的API密钥
        weight (int, optional): 更新的权重
        status (str, optional): 更新的状态
        reset_failures (bool, optional): 是否重置故障计数
        
    返回:
        JSON: 操作结果
    """
    try:
        data = request.get_json()
        
        success, message = update_backend(backend_id, data)
        
        if success:
            return jsonify({"status": "success", "message": message})
        else:
            return jsonify({"error": message}), 400
    except Exception as e:
        log_info("更新后端失败", {"error": str(e), "backend_id": backend_id})
        return jsonify({"error": str(e)}), 500

@backend_api.route("/backends/<backend_id>", methods=["DELETE"])
def delete_backend_handler(backend_id):
    """
    删除后端
    
    参数:
        backend_id (str): 后端ID
        
    返回:
        JSON: 操作结果
    """
    try:
        success, message = delete_backend(backend_id)
        
        if success:
            return jsonify({"status": "success", "message": message})
        else:
            return jsonify({"error": message}), 400
    except Exception as e:
        log_info("删除后端失败", {"error": str(e), "backend_id": backend_id})
        return jsonify({"error": str(e)}), 500 