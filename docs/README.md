# NavCaster docs 目录说明

更新时间：2026-06-13

本目录主要保存协议资料、早期需求、历史计划和草稿素材。除协议原文外，默认
不能把本目录内容当作当前实现事实；实现状态优先参考 `doc`、源码和团队任务卡。

## 当前分组

```text
协议参考
  NTRIP2.0.pdf
  ntrip-protocol-reference.md
  Ntrip2.0.json
  proxy-protocol.txt

早期需求和历史架构
  功能要求.md
  architecture_and_optimization.md
  redis.txt

前端迁移历史
  casterweb-progress.md

proto 生成备忘
  build-pb.txt

草稿代码或历史素材
  client_grid.cpp
  client_grid.h
  server_grid.cpp
  server_grid.h
  monitor.txt
```

## 使用规则

```text
需要协议细节时，可以优先参考协议原文和整理稿，但仍要和当前源码实现核对。
需要当前架构事实时，不从本目录推断，优先看 doc\README.md 给出的阅读顺序。
需要 Redis schema 时，以 doc\redis-schema-v2.md 和源码为准，redis.txt 只作历史参考。
需要 Web 当前结构时，以 web 目录和 doc\project-memory.md 为准，casterweb-progress.md 只作迁移历史。
草稿代码不参与当前构建，不能直接复制进实现，必须另建任务和设计。
```
