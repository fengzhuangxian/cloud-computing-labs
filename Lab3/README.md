# 实验 3：Raft KV 存储系统

进入从实验仓库克隆的文件夹，并拉取最新提交：

`git pull`  

实验3  的指导说明位于 Lab3/README.md 文件中，所有实验材料均存放在 Lab3 / 文件夹内。

## 1. 实验概述

在本实验中，您将实现一个基于 Raft 共识算法的分布式内存键值数据库（KV 存储）。该系统将能够处理客户端的请求，在多个服务器节点之间保持数据一致性，同时能够应对节点故障。


系统应具备以下功能：

- 实现基本的键值存储操作：SET、GET、DEL
- 使用 Raft 协议维护多节点间的数据一致性
- 处理节点故障（更高的要求是能处理网络分区）
- 实现客户端请求的正确重定向

**任务点：补充函数代码**

```
文件路径：Lab3\src\core\raft_core.cpp

std::unique_ptr<Message> RaftCore::handleRequestVote(int from_node_id, const RequestVoteRequest& request)

void RaftCore::handleRequestVoteResponse(int from_node_id, const RequestVoteResponse& response) 

std::unique_ptr<Message> RaftCore::handleAppendEntries(int from_node_id, const AppendEntriesRequest& request) 

void RaftCore::handleAppendEntriesResponse(int from_node_id, const AppendEntriesResponse& response) 
```

## 2. Raft

2PC 协议在协调者永久故障时无法处理客户端请求，因此需引入 Raft 等高级共识协议。Raft 通过选举领导者（Leader）处理客户端请求，并通过日志复制确保节点间数据一致。

## 3. 数据库交互格式

### 3.1 客户端请求消息格式

客户端向服务器发送命令使用 RESP Arrays 格式。RESP Arrays 由以下部分组成：

- 一个 `*` 字符作为第一个字节，后跟数组中元素的数量（十进制数字），然后是 CRLF。
- 任意数量的 bulk strings（长度最多可达 512 MB）。

Bulk string 由以下部分组成：

- 一个 `$` 字节，后跟组成字符串的字节数（前缀长度），以 CRLF 结尾。
- 实际的字符串数据。
- 最后的 CRLF。

例如，字符串 `CS06142` 编码如下：

`$7\r\nCS06142\r\n`

Bulk string `$7\r\nCS06142\r\n` 以 `$` 字节开头，后面的数字 7 表示字符串 `CS06142` 的长度为 7。接下来依次是终止符 CRLF，实际的字符串数据 `CS06142` 和最后的 CRLF。

### 3.2 服务器响应消息格式

#### 1) 成功消息 (Success message)

成功消息按以下方式编码：一个加号 '+' 字符，后跟一个不包含 CR 或 LF 字符（不允许换行）的字符串，以 CRLF 结尾（即 "\r\n"）。

例如，`SET` 命令成功时只需返回 "OK"：

`+OK\r\n`

#### 2) 错误消息 (Error message)

与成功消息类似，错误消息由：一个减号 '-' 字符，后跟一个字符串，以 CRLF 结尾。

例如，如果发生错误，返回：

`-ERROR\r\n`

#### 3) RESP Arrays 消息

当 `GET` 命令成功执行时，服务器应返回 RESP Arrays 消息。

例如：

`*2\r\n$5\r\nCloud\r\n$9\r\nComputing\r\n`

#### 4) 整数消息 (Integer message)

某些命令需要返回整数（例如，`DEL` 命令）。整数消息只是一个表示整数的 CRLF 结尾字符串，前面加上一个 ":" 字节。

整数消息示例：

`:1\r\n`

### 3.3 数据库命令

**注意：`key & value` 约定**

在介绍具体指令规范之前，我们首先约定字符串规范。
在实验中使用的命令中，可能附带两种类型的字符串：**key** 和 **value**。

**key**：用作数据库索引的字符串，规定该字符串不包含空格 `' '`。

**value**：用作数据库内容的字符串，规定该字符串可以包含空格 `' '`。

#### 3.3.1 SET 命令

`SET key value`

`SET` 命令的功能是将 **key** 设置为持有字符串 **value**。如果 key 已经持有一个值，它将被覆盖。例如，如果您想设置 key `CS06142` 持有字符串 `Cloud Computing`，命令将是：

`SET CS06142 "Cloud Computing"`

根据我们在 `section 2.1` 中讨论过的消息格式，此命令将编码为 RESP 消息格式：

`*4\r\n$3\r\nSET\r\n$7\r\nCS06142\r\n$5\r\nCloud\r\n$9\r\nComputing\r\n`

**注意：** 编码后的消息以 `*` 字节开头。后面的数字 4 表示此消息中有 4 个 bulk strings。这些 bulk strings 是 `$3\r\nSET\r\n`，`$7\r\nCS06142\r\n`，`$5\r\nCloud\r\n` 和 `$9\r\nComputing\r\n`。

如果 `SET` 操作成功，服务器将返回一个成功消息；否则，返回错误消息。

例如，如果 `SET` 操作成功，服务器只返回：

`+OK\r\n`

否则，返回：

`-ERROR\r\n`

#### 3.3.2 GET 命令

`GET key`

`GET` 命令可以获取 **key** 的 **value**。如果 key 不存在，则返回特殊值 `nil`。

例如，如果您想检查 key `CS06142` 的值，构造如下命令：

`GET CS06142`

与 `GET` 命令一样，此命令在发送到服务器之前应编码为特定的消息格式。

`*2\r\n$3\r\nGET\r\n$7\r\nCS06142\r\n`

如果 `GET` 命令正确执行，将返回 key 的值（以 RESP Arrays 格式编码）。

例如，如果上述命令正确执行，返回：

`*2\r\n$5\r\nCloud\r\n$9\r\nComputing\r\n`

，假设 key `CS06142` 的值是 `Cloud Computing`。

如果 key 不存在，只需返回：

`*1\r\n$3\r\nnil\r\n`

如果发生错误，返回错误消息：

`-ERROR\r\n`

#### 3.3.3 DEL 命令

`DEL key1 key2 ...`

`DEL` 命令用于删除一个或多个指定的 **keys**（任意数量，最多 512 MB 消息长度）。如果 key 不存在，则忽略。

`DEL` 命令应返回被删除的 keys 的数量。

例如，如果您想删除 key `CS06142` 和 key `CS162`，您可以构造 `DEL` 命令：

`DEL CS06142 CS162`

类似于 `SET` 和 `GET` 命令，`DEL` 命令将以 RESP 消息格式编码如下：

`*3\r\n$3\r\nDEL\r\n$7\r\nCS06142\r\n$5\r\nCS162\r\n`

服务器将返回被删除的 keys 的数量。

例如，如果上述 `DEL` 命令执行，返回一个整数消息：

`:1\r\n`

**注意：** 因为我们假设只设置了 key `CS06142` 持有一个值。至于 key `CS162`，因为它不存在所以会被忽略。所以，整数消息中的数字是 1。

如果发生错误，返回错误消息：

`-ERROR\r\n`

### 3.4 特殊响应类型

由于 Raft 协议的特性，在某些情况下服务器可能返回特殊响应：

#### TRYAGAIN 响应

当服务器暂时无法处理请求时（例如在选举过程中）返回此响应。

- **格式**: `+TRYAGAIN\r\n`
- **客户端处理**: 客户端应稍后重试请求或尝试连接其他节点。

#### MOVED 响应

当请求发送到非 Leader 节点，且该节点知道当前的 Leader 是谁时返回此响应。

- **格式**: `+MOVED <leader_id>\r\n` (其中 `<leader_id>` 是 Leader 节点的 ID)
- **客户端处理**: 客户端应将请求重定向到指定的 Leader 节点。

## 4. 配置文件格式

服务器节点通过配置文件获取集群信息。配置文件格式如下：

```
# Raft集群配置
# 格式：follower_info IP:PORT
# 节点按照在配置文件中的顺序分配ID，从1开始

# 节点1 - 客户端端口8001，Raft端口9001
follower_info 192.168.66.201:8001

# 节点2 - 客户端端口8002，Raft端口9002
follower_info 192.168.66.202:8002

# 节点3 - 客户端端口8003，Raft端口9003
follower_info 192.168.66.203:8003
```

- 以 `#` 开头的行为注释
- `follower_info IP:PORT` 定义集群中的节点：
  - `IP`: 节点的 IP 地址
  - `PORT`: 节点对客户端提供服务的端口号
- 节点 ID 根据配置文件中 `follower_info` 的顺序从 1 开始分配


## 5. 编译与运行

### 5.1 编译

在项目根目录下执行以下命令进行编译：

```bash
make
```

编译成功后将在根目录生成可执行文件 `kvstoreraftsystem`。

### 5.2 运行服务器节点

通过以下命令启动一个服务器节点：

```bash
./kvstoreraftsystem --config_path <config_file_path>
```

例如：

```bash
./kvstoreraftsystem --config_path ./conf/follower1.conf
```

每个节点都需要独立启动，指定其对应的配置文件。

## 6. 测试

项目提供了测试脚本 `lab3_testing.sh` 用于自动化测试。

### 6.1 测试脚本使用方法

具体可见：https://github.com/LabCloudComputing/CloudComputing_Lab3_tester

```bash
./lab3_testing.sh [source_folder] [your_sudo_password] [result_folder] [version]
```

参数说明：

- `<source_folder>`: 项目的根目录绝对路径
- `<your_sudo_password>`: 用户密码
- `<result_folder>`:存放测试结果的文件目录
- `<version>`: 测试标识符，2是Raft版本

示例：

```bash
./lab3_testing.sh ./Lab3 XXXXXX ./Lab3 2
```

### 6.2 测试前准备

在运行测试脚本前，请确保：

- 项目已成功编译
- 配置文件已按测试要求准备好
- 没有其他程序占用测试所需的端口

### 6.3 测试结果

测试脚本执行后会生成结果文件，记录每个测试用例的通过情况。成功的测试将标记为 PASS，失败的测试会显示具体的错误原因。

## 7. 实验要求

1. 实现一个能正确处理 SET、GET、DEL 命令的分布式 KV 存储系统
2. 使用 Raft 协议确保多节点间的数据一致性
3. 正确处理特殊响应（TRYAGAIN、MOVED）
4. 系统应能适当处理节点故障和恢复
5. 通过所有测试用例 （测试用例10 不一定每次都可以通过，有一次成功就可以）

## 8.提交要求

1. 补全指定位置代码
2. 在Lab3文件夹（而不是Lab3的子文件夹下！）下必须有一个文件名为“小组名称+Lab3报告“的文件，此文件内需有通过测试的截图。 （助教会随机抽查一些小组，检验代码能否通过测试）
   实验报告放相应截图就可以（必要），也可以把自己的一些实现的思考过程以及有价值的点写上去（非必要）。

## 9.评分标准

- **测试通过前九个测试单元**：20 分（满分）。
- 部分完成按比例给分。
