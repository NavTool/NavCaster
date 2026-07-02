# NavCaster 当前代码工作流程

> 范围：基于当前 `team-dev` 工作树，覆盖 NTRIP 接入、CasterCore 数据分发、Redis 持久化、HTTP API、SSE 实时推送、Web 前端的端到端流程。
> 本轮复核基线：NC-035 基于 `team-dev @ 72c7e3a`，并纳入 NC-029 至 NC-034 近期文档记忆
> 复核时间：2026-06-16
>
> 说明：本文优先描述当前运行事实。若与旧计划文档或 `references/`、`archive/` 中的历史资料冲突，以源码和本文为准。

---

## 1. 进程启动与线程模型

```
进程入口 (CasterService)
   └─ ntrip_caster::start()                        // 主线程
       ├─ event_base_new()       _base             // 主 base：NTRIP + Caster + Auth + Queue
       ├─ event_base_new()       _http_base        // 独立 base：HTTP API + SSE + sync_redis
       ├─ evthread_use_windows_threads() / evthread_use_pthreads()
       ├─ evthread_make_base_notifiable(_base)
       ├─ evthread_make_base_notifiable(_http_base)
       ├─ AUTH::Init / QUEUE::Init / CASTER::Init   // 主线程组件
       ├─ ntrip_listener::start()
       ├─ Set_Node_Runtime_Info(listen_port, http_port, pid)
       └─ pthread_create(_http_thread)              // HTTP 线程
              ├─ sync_redis::instance().init(_http_base)
              ├─ http_handler::init(_http_base)
              │     ├─ evhttp_new(_http_base)
              │     ├─ register_handlers()          // ~50 handler
              │     └─ sse_manager::init(_http_base, 2s)
              └─ event_base_dispatch(_http_base)
```

主线程 `_base` 与 HTTP 线程 `_http_base` **完全隔离**：
- 主线程持有 `_pub_context` / `_sub_context` 两个 hiredis async 上下文，用于 CasterCore 集群通信
- HTTP 线程持有独立的 `sync_redis`（同步 hiredis）和 `redis_adapter`（异步 hiredis），互不干扰
- HTTP gating 由 `Http_Gate_Callback`（5s 周期）控制：仅当本节点为 Master 或配置 `force_enable=true` 才创建 evhttp listener；一旦创建不再销毁。部署入口策略见 `deployment/http-ingress.md`

---

## 2. NTRIP 接入与会话生命周期

### 2.1 接入流程

```
ntrip_listener (_base, 默认 :4202)
   └─ accept → connect_bev::create()                  // bufferevent
        └─ Read_Cb → ntrip_request::parse()           // 解析 SOURCE / GET 请求
             └─ QUEUE::Push(request)                  // 投递到 ProcessQueue
                  └─ Request_Process_Cb (_base)
                       ├─ AUTH::Verify(account, pwd)  // → Caster_Auth Redis
                       └─ carrier_session::create(...)
                            ├─ ServerCarrier  (基站, SOURCE/POST)
                            ├─ ClientCarrier  (移动站, GET)
                            └─ ProxyCarrier   (上行代理回源)
```

### 2.2 数据分发：CasterCore Pub/Sub

```
基站(ServerCarrier) → CASTER::Pub_Base_Data(login_mpt, data)
   └─ caster_internal::pub_base_data
        ├─ 本地分发到 _client_status_map[mpt 订阅者]
        └─ Redis PUBLISH: caster::raw::<login_mpt>      // 跨节点

移动站(ClientCarrier) → CASTER::Sub_Raw_Data(mpt, callback)
   └─ caster_internal::sub_raw_data
        ├─ 本地命中：直接回调
        └─ 否则：Redis SUBSCRIBE caster::raw::<mpt>
```

别名挂载点（ALIAS:RULE）：通过 `sub_alias_channel` 把订阅指向真实源 mpt。

### 2.3 离线落盘

`carrier_session` 析构 → `flush_online_history`：
- 更新 `STR:STAT` 删除该 uid
- 写入 `LOG:MPT:<login_mpt>` 或 `LOG:USR:<account>` 的 hash field：
  `field = "<connect_time>_<connect_key>"`
  `value = JSON{ disconnect_time, send_total, recv_total, ... }`

---

## 3. 集群与 Master 选举

```
caster_internal 周期性 (5s) 任务 (_keep_alive_event):
   ├─ 收集本机 cpu/mem/queue_delay/server_count/client_count/网络速率
   ├─ HSET CASTER:NODE <node_id> <CasterNode JSON>
   ├─ try_set_master_node():
   │    GET CASTER:MASTER → callback Redis_SetMaster_Callback
   │       └─ SET CASTER:MASTER <node_id> IFEQ <node_id> EX 15
   │            → Redis_KeepMaster_Callback 判定续期成功
   ├─ record_node_history()    // 三层时间序列
   └─ cleanup_stale_history()  // SCAN 清理掉线节点的 NODE:HISTORY
```

- **稳定 Node ID**：`Set_Node_Runtime_Info` 把 `hostname:listen_port:http_port` 哈希为 `Node_XXXXX`，保证重启不变
- **Master TTL**：15 s；若 `IFEQ` 不匹配（其他节点已抢占），则放弃续期，本机降级
- Redis 命令要求：Master 续约依赖 `SET ... IFEQ ... EX`，运行态状态依赖
  `HSETEX`/`HEXPIRE`；部署最低版本见 `deployment/redis.md`
- 主节点变更事件写入 `LOG:NODE:<id>` (event=`master_acquired` / `master_lost`)

---

## 4. 节点历史三层时间序列

| 层级 | Redis Key | 粒度 | 容量 | 保留 |
|------|-----------|------|------|------|
| RAW | `NODE:HISTORY:<id>` | 5 s | 120960 | 7 天 |
| 1MIN | `NODE:HISTORY:<id>:1M` | 60 s | 33120 | 23 天 |
| 5MIN | `NODE:HISTORY:<id>:5M` | 5 min | 96480 | 335 天 |

聚合策略（`record_node_history`）：
- 每写入一条 RAW 即 LPUSH+LTRIM；
- `_1min_agg_counter` 累计 12 条 → 平均后写入 1MIN；
- `_5min_agg_counter` 累计 5 条 1MIN → 平均后写入 5MIN。

字段：`t / cpu / mem / mpt / usr / conn / send_speed / recv_speed / send_total / recv_total / q_delay`。

前端 `NodeDetail` 通过 `?range=raw|1m|5m` 拉取相应层级。

---

## 5. HTTP API 与 SSE

### 5.1 同步路径（HTTP 线程）

```
evhttp_request → Dispatch_Cb (_http_base)
   ├─ http_handler::route_request(req)
   │    ├─ CORS 处理 / OPTIONS preflight
   │    ├─ Authorization Bearer + token 验证
   │    └─ 路由匹配 → 具体 handler
   └─ handler 中通过 sync_redis::instance() 直接调用 Redis (HGETALL/SET/SCAN/INFO/...)
```

数据来源大多是 Redis hash/list；个别接口（如 `handle_fetch_sourcetable`）会发起远端 TCP，由 HTTP 线程独立承载，主线程不受阻塞。

### 5.2 实时推送（SSE）

```
GET /api/events/stream?token=<token>&channels=servers,clients,nodes
   └─ http_handler::handle_sse_stream
        └─ sse_manager::add_client(req, channels)
             ├─ Content-Type: text/event-stream
             ├─ evhttp_send_reply_start
             ├─ evhttp_connection_set_closecb(on_client_close)
             └─ 立即发送各订阅 channel 当前快照

每 2 s on_timer (_http_base):
   ├─ 遍历当前有订阅者的已注册 channel：
   │    new_data = fetcher();    (从 Redis 拉一次 HGETALL / snapshot)
   │    if (new_data != cached_data) broadcast
   └─ 向所有 client 发 ":keepalive" 注释
```

已注册 channel：`servers / clients / streams / nodes / accounts / sources / aliases / access_groups / pull_records / pull_states / push_records / push_states / account_actives`。

---

## 6. 前端数据流

```
Login.tsx → setBaseURL(http://host:port) + login() → Bearer token 存 localStorage
   └─ MainLayout → 嵌套路由
        ├─ Dashboard / Servers / Clients ...   useSSE/useMultiSSE → EventSource
        ├─ NodeDetail                          getNodeHistory(range) → recharts
        ├─ SystemMonitor                       getMonitorRedis + getMonitorCluster (轮询)
        └─ AccountDetail / Settings ...        usePolling(api, interval)
```

- **useSSE**：单 channel；`useMultiSSE`：多 channel 单连接；网络断开 3 s 后重连
- **DataTable**：通用表格组件，内置搜索 / 列控制 / CSV 导出
- **样式**：Ant Design 5 暗色主题（`#141625` 背景，`#2e3450` 边框，`#4a8eff` 主色）

---

## 7. Redis 键命名总览

| Key | 类型 | 用途 |
|-----|------|------|
| `MPT:LIST` | Hash | 在线挂载点登记 (`ServerState`) |
| `USR:STAT` | Hash | 在线移动站 (`ClientState`) |
| `STR:STAT` | Hash | 数据流统计 (`StreamState`) |
| `ACT:SESSION:<account>` | Hash | 实名账号展示会话 |
| `STR:ACTIVE` | Hash | legacy 账号活跃 fallback |
| `ACT:ACTIVE` | Hash | 登录索引，不表示在线会话 |
| `MPT:SUB` | Hash | 挂载点订阅关系 |
| `LOG:MPT:<mount>` | Hash | 基站连接历史，field=`<ts>_<key>` |
| `LOG:USR:<account>` | Hash | 移动站连接历史 |
| `LOG:NODE:<id>` | Hash | 节点事件日志（master 切换等） |
| `CASTER:NODE` | Hash | 集群节点状态（每节点一字段） |
| `CASTER:MASTER` | String | Master 锁 (TTL 15 s) |
| `NODE:HISTORY:<id>[:1M\|:5M]` | List | 节点历史时间序列 |
| `PULL:RECORD` / `PULL:STAT` | Hash | 上行拉流配置 / 状态 |
| `PUSH:RECORD` / `PUSH:STAT` | Hash | 推流配置 / 状态 |
| `ALIAS:RULE` | Hash | 挂载点别名映射 |
| `ACCESS:GROUP` / `ACCESS:ITEM:<g>` | Hash | 访问控制组 / 条目 |
| `ACT:RECORD` | Hash | 账号信息 |
| `CONF:SERVICE` / `CONF:CORE` / `CONF:AUTH` | Hash | 配置（Redis 化） |
| `STAT:DAILY:<yyyymmdd>` | Hash | 每日统计缓存 |
| `MONITOR:REDIS:HISTORY` | List | Redis 监控历史（计划中） |

---

## 8. 构建与运行

```bash
# 编译
.\deploy\scripts\build_ninja.ps1 -BuildType Release
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target CasterService

# Linux
BUILD_TYPE=Release bash deploy/scripts/build_ninja.sh --target CasterService

# 启动
cd bin/Debug && ./CasterService

# 前端开发
cd app/web && npm ci && npm run dev

# 前端类型检查
cd app/web && npx tsc --noEmit
```

依赖：`libevent2 / hiredis / protobuf / nlohmann_json / spdlog / yaml-cpp`，均通过 `third_party/` 子模块提供。
