# My-project

个人项目作品集。

## 项目一：Hound — 高性能 HTTP/1.1 服务器

**技术栈：** C++17、Reactor 模式、协程、HTTP/1.1 协议

从零实现的轻量级 HTTP 服务器，包含 Reactor 事件循环、协程框架和手写 HTTP 协议解析器。5000+ 行代码，零第三方依赖。详见 [`hound/`](hound/)。

**核心模块：**
- I/O 抽象层：RAII Socket、链式 Buffer（零拷贝）
- Reactor 引擎：select/epoll 多路复用、O(1) 时间轮、one loop per thread
- 协程框架：Windows Fibers / POSIX ucontext 上下文切换、协程化 I/O
- HTTP/1.1：RFC 7230 状态机解析器、Trie 路由、REST API

## 项目二：校园信息综合管理系统

**技术栈：** Rust、eframe/egui、serde、CSV、JSON/AOF 双持久化

独立开发的单机桌面校园信息管理工具，详见 `master` 分支。
