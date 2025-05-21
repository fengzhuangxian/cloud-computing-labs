"""
RESP协议处理模块 - 负责RESP协议编解码和Raft集群通信
"""
import json
import re
import socket
import random
from utils.logger import log_info
from config import RAFT_HOST, RAFT_PORTS, RAFT_TIMEOUT

# 当前使用的Raft节点端口
current_port = RAFT_PORTS[0] if RAFT_PORTS else 8001
# 上次工作的端口
last_working_port = current_port

def resp_decode(data):
    """
    解码RESP协议数据为Python对象
    
    参数:
        data (str): RESP协议格式的数据
        
    返回:
        解码后的Python对象(通常是字符串、列表或字典)
    """
    if not data:
        return None
    
    try:
        # 如果数据是空字符串或仅包含空白，则返回None
        if not data.strip():
            return None
        
        # 记录原始数据用于调试
        log_info("开始解码RESP数据", {"data_length": len(data), "data_prefix": data[:50] if len(data) > 50 else data})
            
        # 如果数据以$开头，这是Bulk String
        if data.startswith("$"):
            try:
                # 查找第一个\r\n位置
                first_break = data.find("\r\n")
                if first_break < 0:
                    log_info("RESP解码错误：找不到第一个换行符", {"data_preview": data[:100]})
                    return None
                
                # 提取长度
                length_str = data[1:first_break]
                length = int(length_str)
                
                # 如果是NULL值 ($-1)
                if length < 0:
                    return None
                
                # 计算内容起始和结束位置
                content_start = first_break + 2  # 跳过\r\n
                content_end = content_start + length
                
                # 检查结束位置是否超出数据范围
                if content_end > len(data):
                    log_info("RESP解码错误：内容长度超出数据范围", 
                             {"declared_length": length, "actual_length": len(data) - content_start})
                    # 仍然尝试提取可用内容
                    content = data[content_start:]
                else:
                    # 提取内容
                    content = data[content_start:content_end]
                
                # 检查内容是否是JSON
                if ((content.startswith("[") and content.endswith("]")) or 
                    (content.startswith("{") and content.endswith("}"))):
                    try:
                        return json.loads(content)
                    except json.JSONDecodeError as e:
                        log_info("JSON解析错误", {"error": str(e), "content_sample": content[:100]})
                
                return content
            except Exception as e:
                log_info("RESP Bulk String解码错误", {"error": str(e), "data_sample": data[:100]})
                # 失败后尝试直接提取JSON
                json_match = re.search(r'(\[.*\]|\{.*\})', data, re.DOTALL | re.UNICODE)
                if json_match:
                    try:
                        return json.loads(json_match.group(0))
                    except:
                        pass
                return data
                
        # 如果是数组类型 (*...)
        elif data.startswith("*"):
            parts = data.split("\r\n")
            if len(parts) < 2:
                return data
                
            # 尝试提取JSON
            for i, part in enumerate(parts):
                if part.startswith("$") and i+1 < len(parts):
                    try:
                        # 检查是否是JSON格式
                        if ((parts[i+1].startswith("[") and parts[i+1].endswith("]")) or
                            (parts[i+1].startswith("{") and parts[i+1].endswith("}"))):
                            return json.loads(parts[i+1])
                    except:
                        pass
            
            # 使用正则表达式尝试直接提取完整的JSON
            json_match = re.search(r'(\[.*\]|\{.*\})', data, re.DOTALL | re.UNICODE)
            if json_match:
                json_str = json_match.group(0)
                try:
                    return json.loads(json_str)
                except:
                    log_info("JSON解析失败", {"json_str": json_str[:100]})
            
            return data
        
        # 如果数据本身是JSON格式
        if ((data.strip().startswith("[") and data.strip().endswith("]")) or 
            (data.strip().startswith("{") and data.strip().endswith("}"))):
            try:
                return json.loads(data.strip())
            except:
                pass
                
        # 无法识别的格式，返回原始数据
        return data
    except Exception as e:
        log_info("RESP解码通用错误", {"error": str(e), "data_type": type(data).__name__, "data_sample": str(data)[:100]})
        return data

def resp_encode(value):
    """
    将Python对象编码为RESP协议数据
    
    参数:
        value: 要编码的Python对象
        
    返回:
        str: RESP协议格式的数据
    """
    try:
        if value is None:
            return "$-1\r\n"
        
        if isinstance(value, str):
            # 确保字符串正确编码，尤其是中文字符
            value_bytes = value.encode('utf-8')
            return f"${len(value_bytes)}\r\n{value}\r\n"
        
        # 处理JSON对象
        if isinstance(value, (dict, list)):
            json_str = json.dumps(value, ensure_ascii=False)
            json_bytes = json_str.encode('utf-8')
            return f"${len(json_bytes)}\r\n{json_str}\r\n"
        
        # 其他类型转为字符串
        value_str = str(value)
        value_bytes = value_str.encode('utf-8')
        return f"${len(value_bytes)}\r\n{value_str}\r\n"
    except Exception as e:
        log_info("RESP编码错误", {"error": str(e), "value_type": type(value).__name__})
        return "$-1\r\n"

def send_resp_command(cmd, host=None, port=None, retry_count=0):
    """
    发送Redis协议命令到Raft服务器，处理特殊响应和超时
    
    参数:
    - cmd: 发送的RESP命令
    - host: 服务器主机名，默认使用配置中的RAFT_HOST
    - port: 服务器端口号，如果为None则使用当前活跃端口
    - retry_count: 当前重试次数，用于防止无限递归
    
    返回:
    - 服务器响应或错误消息
    """
    global current_port, last_working_port
    
    # 如果未指定主机，使用配置的主机
    if host is None:
        host = RAFT_HOST
    
    # 如果未指定端口，使用当前活跃端口
    if port is None:
        port = current_port
    
    # 防止无限重试
    if retry_count >= len(RAFT_PORTS):
        return "$-1\r\nERROR:所有Raft节点均无法连接，请检查服务器状态\r\n"
    
    # 记录命令信息
    log_info("发送RESP命令", {"host": host, "port": port, "cmd_length": len(cmd), "cmd_preview": cmd[:50] if len(cmd) > 50 else cmd})
    
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        # 设置连接超时
        s.settimeout(RAFT_TIMEOUT)
        s.connect((host, port))
        
        # 确保命令正确编码，特别是对中文内容
        if isinstance(cmd, str):
            cmd_bytes = cmd.encode('utf-8')
        else:
            cmd_bytes = cmd
            
        s.sendall(cmd_bytes)
        
        # 初始化接收的数据
        received_data = b''
        buffer_size = 8192  # 更大的缓冲区
        
        # 持续接收数据，分批次读取避免超大响应
        while True:
            try:
                chunk = s.recv(buffer_size)
                if not chunk:
                    break
                received_data += chunk
                if len(chunk) < buffer_size:
                    break
            except socket.timeout:
                if received_data:  # 如果已收到部分数据，认为完成
                    break
                raise  # 否则重新抛出超时异常
        
        # 尝试多种编码方式解码
        result = None
        for encoding in ['utf-8', 'gbk']:
            try:
                result = received_data.decode(encoding, errors='replace')
                break  # 成功解码后退出循环
            except UnicodeDecodeError:
                continue
        
        # 如果所有编码都失败，使用utf-8并忽略错误
        if result is None:
            result = received_data.decode('utf-8', errors='ignore')
        
        # 记录响应信息
        log_info("接收到RESP响应", {
            "response_length": len(result), 
            "response_preview": result[:50] if len(result) > 50 else result
        })
        
        # 连接成功，更新最后工作的端口
        last_working_port = port
        current_port = port
        
        # 处理MOVED响应
        moved_match = re.search(r'\+MOVED (\d+)', result)
        if moved_match:
            node_id = int(moved_match.group(1))
            if 1 <= node_id <= len(RAFT_PORTS):
                new_port = RAFT_PORTS[node_id-1]  # 例如，节点1对应RAFT_PORTS[0]
                current_port = new_port
                # 使用新端口重试
                s.close()
                return send_resp_command(cmd, host, new_port, retry_count + 1)
        
        # 处理TRYAGAIN响应
        if '+TRYAGAIN' in result:
            return "$-1\r\nERROR:服务器出现了小差错，请稍后再试\r\n"
            
        return result
    except socket.timeout:
        log_info("连接到Raft节点超时", {"host": host, "port": port})
        
        # 尝试其他端口，先从上次工作的端口开始
        next_ports = [p for p in RAFT_PORTS if p != port]
        if last_working_port in next_ports:
            # 优先尝试上次成功的端口
            next_ports.remove(last_working_port)
            next_ports.insert(0, last_working_port)
            
        if next_ports:
            s.close()
            next_port = next_ports[0]
            current_port = next_port
            # 递归调用，尝试下一个端口
            return send_resp_command(cmd, host, next_port, retry_count + 1)
        
        return "$-1\r\nERROR:连接超时，无法连接到Raft集群\r\n"
    except ConnectionRefusedError:
        log_info("连接到Raft节点被拒绝", {"host": host, "port": port})
        
        # 类似超时处理，尝试下一个可用端口
        next_ports = [p for p in RAFT_PORTS if p != port]
        if next_ports:
            s.close()
            next_port = next_ports[0]
            current_port = next_port
            return send_resp_command(cmd, host, next_port, retry_count + 1)
            
        return "$-1\r\nERROR:连接被拒绝，无法连接到Raft集群\r\n"
    except Exception as e:
        log_info("与Raft节点通信出错", {"error": str(e)})
        return f"$-1\r\nERROR:{str(e)}\r\n"
    finally:
        s.close()

def raft_get(key, host=None, port=None):
    """
    从Raft KV存储中获取键值
    
    参数:
        key (str): 键名
        host (str): Raft服务器主机
        port (int): Raft服务器端口，默认自动选择
        
    返回:
        str: RESP协议格式的响应，或者解析后的Python对象
    """
    try:
        # 确保key是字符串
        key_str = str(key)
        # 获取UTF-8编码下的字节长度
        key_bytes = key_str.encode('utf-8')
        key_length = len(key_bytes)
        
        # 构造GET命令
        cmd = f"*2\r\n$3\r\nGET\r\n${key_length}\r\n{key_str}\r\n"
        
        # 发送命令
        resp_result = send_resp_command(cmd, host, port)
        
        # 尝试解码响应
        decoded = resp_decode(resp_result)
        
        # 记录结果
        log_info("RAFT GET结果", {
            "key": key_str,
            "result_type": type(decoded).__name__ if decoded is not None else "None",
            "result_preview": str(decoded)[:100] if decoded is not None else "None"
        })
        
        return decoded if decoded is not None else resp_result
    except Exception as e:
        log_info("执行GET命令出错", {"error": str(e), "key": key})
        return "$-1\r\nERROR:执行GET命令出错\r\n"

def raft_set(key, value, host=None, port=None):
    """
    向Raft KV存储设置键值
    
    参数:
        key (str): 键名
        value (str|dict|list): 值内容，可以是字符串或JSON对象
        host (str): Raft服务器主机
        port (int): Raft服务器端口，默认自动选择
        
    返回:
        str: RESP协议格式的响应
    """
    try:
        # 确保key是字符串
        key_str = str(key)
        
        # 处理不同类型的value
        if isinstance(value, (dict, list)):
            # 对JSON进行序列化，确保中文字符不被转义
            value_str = json.dumps(value, ensure_ascii=False)
            
            # 记录JSON数据
            log_info("正在存储JSON数据", {
                "key": key_str, 
                "json_type": type(value).__name__, 
                "json_length": len(value_str)
            })
        else:
            # 非JSON数据转为字符串
            value_str = str(value)
        
        # 获取UTF-8编码下的字节长度
        key_bytes = key_str.encode('utf-8')
        value_bytes = value_str.encode('utf-8')
        key_length = len(key_bytes)
        value_length = len(value_bytes)
        
        # 记录编码后的信息
        log_info("编码SET命令", {
            "key": key_str,
            "key_length": key_length,
            "value_length": value_length,
            "value_preview": value_str[:50] if len(value_str) > 50 else value_str
        })
        
        # 构造SET命令
        cmd = f"*3\r\n$3\r\nSET\r\n${key_length}\r\n{key_str}\r\n${value_length}\r\n{value_str}\r\n"
        
        # 发送命令并返回结果
        result = send_resp_command(cmd, host, port)
        
        # 检查结果状态
        if "+OK" in result:
            log_info("SET命令成功", {"key": key_str})
        else:
            log_info("SET命令可能失败", {"key": key_str, "response": result[:100]})
            
        return result
    except Exception as e:
        log_info("执行SET命令出错", {"error": str(e), "key": key})
        return "$-1\r\nERROR:执行SET命令出错\r\n" 