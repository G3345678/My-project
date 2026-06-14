# 校园信息管理系统
基于 Rust + egui 开发的轻量级桌面校园信息管理工具，采用 JSON 全量快照 + AOF 增量日志双持久化机制。

## 技术栈
- Rust
- eframe / egui（即时模式 GUI）
- serde（序列化/反序列化）
- CSV 批量数据处理

## 核心功能
- 学生信息与成绩管理
- 教师信息管理
- 馆藏图书管理
- 多维度数据统计看板
- JSON / CSV 批量导入导出
- AOF 日志持久化，异常退出数据不丢失

## 运行方式
```bash
cargo run --release
