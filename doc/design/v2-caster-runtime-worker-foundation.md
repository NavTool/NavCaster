# v2 Caster Runtime Worker Foundation

任务：NC-084-v2-caster-runtime-worker-foundation

状态：runtime/worker 可运行骨架

## 目标

本任务创建全新的 C++ `navcaster-caster` 工程骨架，不复用旧 `CasterService`
模块结构、旧 protobuf、旧命名或旧 Redis key。当前实现只建立 Runtime/Worker
生命周期、handoff 所有权边界、worker mailbox、mount owner registry 和本机
health/metrics 输出，不实现完整 NTRIP 业务链路。

## 目录

```text
caster/
  app/              CLI、配置解析、进程入口
  runtime/          RuntimeManager、WorkerManager、MountOwnerRegistry、health/metrics
  transport/        Acceptor、AcceptorSession、HandoffMessage
  worker/           CasterWorker、WorkerCore、WorkerMailbox、WorkerMetrics
  session/          source/client/near/relay session 初始类型
  domain/           ConnectInfo、mount/stream/auth domain 类型
  storage/redis/    每 worker 独立 Redis async context 边界
  infra/            libevent、socket、timer、logger 基础设施
```

## 已实现边界

- `navcaster-caster` CMake target。
- `RuntimeManager` 启动/停止控制 event loop、WorkerManager、Acceptor 和 local API。
- `WorkerManager` 按 `worker_count` 创建多个 `CasterWorker` 线程。
- 每个 `CasterWorker` 拥有独立 `event_base`、mailbox、WorkerCore 和 Redis context 边界对象。
- `WorkerMailbox` 使用 `event_active` 作为跨线程消息入口。
- `MountOwnerRegistry` 维护 `mount -> worker_id`，过滤 draining worker，并按轻量负载选择 owner。
- `AcceptorSession` 只读取请求 header，解析 `ConnectInfo` 后把 fd、initial bytes 和远端地址交给 worker。
- handoff 成功后 acceptor 释放临时 bufferevent 但不关闭 fd；handoff 失败由 acceptor 关闭 fd。
- local API 暴露：
  - `GET /api/v1/runtime-local/health`
  - `GET /api/v1/runtime-local/metrics`
  - 同时保留 `/health` 和 `/metrics` 作为本地开发短路径。
- `--self-test` 会启动 runtime，投递 worker mailbox probe，走一次 mount owner 分配，再停止。

## 未实现边界

- 未实现完整 source/client/near/relay NTRIP session。
- 未实现正式 `bufferevent` session 数据链路。
- 未连接 Redis，也不写任何 Redis key；当前只保留每 worker 独立 Redis context 类型边界。
- 未访问 PostgreSQL。
- 未实现 worker_count 在线调整 local API。
- 未实现本地 token 认证，后续 Agent 对接时补齐。

## 验证入口

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2
```

当前骨架阶段不运行完整系统 smoke，也不要求最小 NTRIP payload 转发。该部分由后续
Caster MVP/NTRIP 数据链路任务补齐。
