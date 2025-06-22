# 《云计算技术》课程实验-云里雾里队

<!--**This is the repo containing all the lab materials for undergraduate course CS06142 "Cloud Computing Techniques" at Hunan University**--> 

## 简介

这是2025年湖南大学 CS06142《云计算技术》课程 云里雾里队 的实验仓库。

四次实验分别主要涉及并发编程、网络编程、Raft分布式一致性算法和前后端全栈开发能力。

最终云里雾里队完成所有实验并上台展示了Lab4，在该课程上获得了100分的成绩。



## Lab1：多线程解大量数独题

实验要求见[Lab1目录](https://github.com/fengzhuangxian/cloud-computing-labs/tree/master/Lab1)

这是一个经典的**生产者-消费者**问题，主要练习**并发编程**。

关键设计：

- 使用**环形缓冲区**存储数独题
- 使用3个指针input_index,solve_index,output_index来标识环形缓冲区中新增未解题，已解题，输出题的位置，并以指针位置为条件处理生产者-消费者的同步问题（采用pthread）
- 使用3个线程分别专门处理输入文件、读取文件中的数独题、输出已解的数独题
- 负责解题的多个工作线程，采用**线程池**方案



## Lab2：高性能 HTTP Server 及 Proxy Server

实验要求见[Lab2目录](https://github.com/fengzhuangxian/cloud-computing-labs/tree/master/Lab2)

## Lab3：基于Raft协议的分布式数据库

实验要求见[Lab3目录](https://github.com/fengzhuangxian/cloud-computing-labs/tree/master/Lab3)

## Lab4：LLM聊天服务系统

实验要求见[Lab4目录](https://github.com/fengzhuangxian/cloud-computing-labs/tree/master/Lab4)
