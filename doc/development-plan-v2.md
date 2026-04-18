# NavCaster 第二期迭代开发方案

> 基于第一期迭代成果，聚焦**使用体验提升、精细控制、Redis 监控、线程分离**四大方向。

---

## 第一期迭代回顾

### 已完成功能

| 模块 | 功能 | 状态 |
|------|------|------|
| 连接历史 | LOG:MPT / LOG:USR HASH 记录、连接历史页面、基站/用户/账户详情历史表格 | ✅ |
| 崩溃恢复 | `flush_online_history()` 补偿断线日志、`cleanup_stale_history()` 清理残留、`component_stop()` 优雅关闭 | ✅ |
| 节点历史 | NODE:HISTORY LIST 5秒采样(24h)、节点详情趋势图(recharts) | ✅ 待升级分级存储 |
| 数据统计 | 概览统计、每小时趋势、基站/用户 TOP 排行、每日统计缓存(STAT:DAILY) | ✅ |
| 多节点同步 | Master TTL 15s、Master 状态跟踪与日志、Dashboard Master 徽章、CASTER:CONF 别名即时同步 | ✅ |
| 日志优化 | 结构化 spdlog 分级日志（部分关键位置） | ✅ 部分 |

### 当前架构概况

```
┌─────────────────── 单线程 event_base ───────────────────┐
│                                                          │
│  NTRIP Listener ← TCP 连接接入                          │
│  CasterCore     ← Redis Async (Pub/Sub, 状态同步)       │
│  AuthVerify     ← Redis Async (认证)                    │
│  ProcessQueue   ← 请求处理管道                          │
│  ConnectBev     ← 连接 bufferevent 管理                 │
│  HTTP Server    ← evhttp (API + 静态文件)               │
│  SSE Manager    ← 2秒轮询推送                           │
│  sync_redis     ← 同步阻塞 Redis (HTTP API 用)         │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

**核心问题**：HTTP/SSE 的同步 Redis 调用阻塞 NTRIP 事件循环。

### 现有 Web 页面

| 页面 | 路径 | 功能 |
|------|------|------|
| 节点状态 | `/dashboard` | 集群节点卡片(CPU/内存/连接数/Master标识) |
| 节点详情 | `/nodes/:id` | 单节点历史趋势图 |
| 基准站 | `/servers` | 在线基站列表(SSE实时) |
| 基站详情 | `/servers/:id` | 连接信息 + 历史连接表 |
| 移动站 | `/clients` | 在线用户列表(SSE实时) |
| 用户详情 | `/clients/:id` | 连接信息 + 历史连接表 |
| 账号管理 | `/accounts` | 账号 CRUD |
| 账号详情 | `/accounts/:id` | 账号信息 + 历史登录表 |
| 源列表 | `/sources` | 挂载点源记录 CRUD |
| 别名管理 | `/aliases` | 别名挂载点 CRUD |
| 访问管理 | `/access` | 访问权限组 + 权限项管理 |
| 源表视图 | `/sourcetable` | 远程/本地 NTRIP 源表查看 |
| 数据接入 | `/relay/pull` | Pull Relay 任务管理 + 启停 |
| 数据推送 | `/relay/push` | Push Relay 任务管理 + 启停 |
| 连接历史 | `/history` | 基站/用户连接日志 |
| 数据统计 | `/statistics` | 概览/趋势/排行 |
| 系统设置 | `/settings` | 密码修改 |

---

## 目录

- [一、HTTP/业务线程分离](#一http业务线程分离)
- [二、节点精细控制](#二节点精细控制)
- [三、Redis 监控与集群状态](#三redis-监控与集群状态)
- [四、前端界面升级](#四前端界面升级)
- [五、详细信息页面增强](#五详细信息页面增强)
- [六、系统设置完善](#六系统设置完善)
- [七、操作日志与安全增强](#七操作日志与安全增强)

---

## 当前进展（2026-04-18）

### 已完成并联调通过

- Phase 1 基础项已完成：HTTP 线程分离、节点历史分级存储、前端主布局与通用监控能力已经落地。
- Phase 2 已完成：`3.1 Redis INFO API`、`3.2 Key 空间分析`、`3.4 集群状态 API`、`5.5 系统监控页`、`2.1 节点配置查看`、`2.2 节点控制基础能力`、`7.1 审计日志后端`、`7.2 操作日志页面`。
- 已验证链路：登录、`/api/monitor/redis`、`/api/nodes/config/{id}`、`/api/nodes/action/{id}`、`/api/logs/audit`；其中 `config_update` 已确认触发 `CASTER:CONF -> CONFIG` 并在节点侧热加载。
- 节点可观测性增强已完成：系统监控页和节点详情页已补充监听端口、HTTP 端口、进程 ID、运行平台、在线时长等运行时信息。
- 节点身份规范已完成收敛：`node_id` 继续保持稳定哈希，展示名称统一调整为 `Node_XXXXX`；HTTP API 默认仅主节点开放，从节点需显式配置 `HTTP_API_Setting.Enable_On_Slave=true` 才会对外监听。

### 当前实现边界

- 节点控制当前已支持：`sync_cluster`、`set_log_level`、`config_update`。
- 审计日志当前已覆盖节点控制链路，其他资源的审计补齐仍可继续扩展。
- 系统监控页当前聚焦 Redis 状态、集群状态、Key 空间分析；Redis 历史趋势依赖 `3.3`，尚未纳入本轮实现。
- HTTP 管理策略当前为“主节点默认开放、从节点按配置开放”；该策略已支持随主从角色变化动态启停 HTTP 线程，但配置项仍以部署配置/YAML 为主。

### 后续工作

- Phase 3 页面增强：`5.1`、`5.2`、`5.4` 以及 `5.3` 中“节点日志”能力仍待完成。
- Phase 4 系统完善：`6.1-6.3` 设置页完善、`7.3` 安全增强、`3.3` Redis 历史趋势、`2.3` 节点日志查看仍未开始。
- 下一步迭代重点：补齐节点日志查看、将 `Enable_On_Slave` 纳入前端配置编辑闭环，并继续扩展审计日志覆盖范围。

---

## 〇、节点历史分级存储

### 目标

将节点历史从当前的单一粒度（5秒/24小时）升级为三级存储策略，覆盖从秒级到年级的完整时间跨度。

### 当前状态

- `NODE:HISTORY:{id}` — LIST，5 秒采样，最多 17280 条（24 小时）
- 超过 24 小时的数据被丢弃

### 分级存储方案

| 级别 | 粒度 | 保留时长 | 最大条目数 | Redis Key |
|------|------|----------|------------|----------|
| RAW | 5 秒 | 7 天 | 120,960 | `NODE:HISTORY:{id}` |
| 1MIN | 60 秒(聚合) | 30 天(7d~30d) | 33,120 | `NODE:HISTORY:{id}:1M` |
| 5MIN | 5 分钟(聚合) | 1 年(30d~365d) | 96,480 | `NODE:HISTORY:{id}:5M` |

总计每个节点约 **250,560 条**记录，约 **25MB** Redis 内存。

### 聚合策略

在 `record_node_history()` 中通过计数器驱动聚合：

```
每 5 秒: LPUSH RAW 快照
每 60 秒(12 个 RAW): 计算 12 个样本的均值 → LPUSH 1M
每 5 分钟(5 个 1M):  计算 5 个样本的均值 → LPUSH 5M
```

**聚合字段**：
- `cpu`, `q_delay` → 取平均值
- `mem`, `mpt`, `usr`, `conn` → 取平均值
- `send_speed`, `recv_speed` → 取平均值
- `send_total`, `recv_total` → 取最后一个值（累计量）
- `t` → 取聚合周期的中间时间戳

### 后端实现

```cpp
// caster_internal.h 新增
#define NODE_HISTORY_RAW_MAX   120960  // 5s × 7天
#define NODE_HISTORY_1M_MAX    33120   // 60s × 23天
#define NODE_HISTORY_5M_MAX    96480   // 5min × 335天

int _1min_agg_counter = 0;     // 每 12 个 RAW 触发 1M 聚合
int _5min_agg_counter = 0;     // 每 5 个 1M 触发 5M 聚合
json _1min_agg_buffer;         // RAW 样本累加器
json _5min_agg_buffer;         // 1M 样本累加器
```

### API 变更

`GET /api/nodes/history/{id}` 增加 `range` 参数：

| range 值 | 数据源 | 说明 |
|----------|--------|------|
| `1h` ~ `7d` | RAW (5s) | 默认行为，从 `NODE:HISTORY:{id}` 读取 |
| `7d` ~ `30d` | 1M (60s) | 从 `NODE:HISTORY:{id}:1M` 读取 |
| `30d` ~ `1y` | 5M (5min) | 从 `NODE:HISTORY:{id}:5M` 读取 |
| 不传 | 自动 | 根据 limit 自动选择层级 |

### 前端变更

`NodeDetail.tsx` 时间范围选择器扩展：

```
1小时 | 6小时 | 24小时 | 7天 | 30天 | 1年
```

对于长时间范围（>7天），前端自动降采样显示，最多 2000 个数据点。

---

## 一、HTTP/业务线程分离

### 目标

将 HTTP API、SSE 推送、静态文件服务从 NTRIP 业务主循环中剥离，运行在独立线程上，互不阻塞。

### 当前问题

1. HTTP handler 中的 `sync_redis` 同步阻塞调用会卡住整个 event loop
2. SSE `DataFetcher` 同步调用 `hgetall()` 阻塞 NTRIP 数据转发
3. `handle_fetch_sourcetable()` 远程 TCP 连接完全阻塞
4. 并发 HTTP 请求会排队处理，响应延迟高

### 方案设计

#### 1.1 双 event_base 架构

```
主线程 (event_base_1):                 HTTP 线程 (event_base_2):
├── ntrip_listener                     ├── evhttp (API 路由)
├── CasterCore (Redis Async)           ├── SSE Manager (定时轮询)
├── AuthVerify (Redis Async)           ├── sync_redis (Core Redis)
├── ProcessQueue                       ├── sync_redis_auth (Auth Redis)
└── ConnectBev                         └── 独立 event loop
```

#### 1.2 实现步骤

#### 1.3 当前落地结果

- HTTP API、SSE、静态文件服务已迁移到独立 `_http_base` 和独立线程运行。
- 服务层已根据主从状态动态控制 HTTP 生命周期：主节点自动开放管理端口，从节点默认不开放，避免同机多节点争抢 `8080`。
- 若需要在从节点提供 HTTP 能力，可在部署配置中设置 `HTTP_API_Setting.Enable_On_Slave=true`。

**Step 1: HTTP 线程独立化**

```cpp
// ntrip_caster.h 新增
std::thread _http_thread;
event_base *_http_base = nullptr;

// ntrip_caster.cpp
void ntrip_caster::extra_init() {
    _http_base = event_base_new();  // 独立 event_base

    // HTTP 组件绑定到 _http_base
    _http_handler.init(_http_base, ...);

    // 启动 HTTP 线程
    _http_thread = std::thread([this]() {
        event_base_dispatch(_http_base);
    });
}
```

**Step 2: 数据访问解耦**

HTTP 线程通过 **Redis 独立连接** 访问数据，不直接调用 CasterCore 接口：
- `sync_redis` 已经是独立同步连接 → 可安全在 HTTP 线程使用
- SSE `DataFetcher` 的 `hgetall()` 调用转移到 HTTP 线程
- 跨线程数据需求通过 Redis Hash 中转（已有机制）

**Step 3: 优雅停止**

```cpp
void ntrip_caster::component_stop() {
    // 1. 停止 HTTP event loop
    event_base_loopbreak(_http_base);
    if (_http_thread.joinable()) _http_thread.join();
    event_base_free(_http_base);

    // 2. 停止业务组件
    ntrip_listener::getInstance()->stop();
    CASTER::Free();
}
```

#### 1.3 关键注意点

| 问题 | 方案 |
|------|------|
| `_active_tokens` + `_token_mutex` | 已有 mutex 保护，线程安全 ✅ |
| `redis_adapter` (async) | 不在 HTTP 线程使用，改用 `sync_redis` |
| `handle_fetch_sourcetable` 远程连接 | 在 HTTP 线程阻塞不影响业务 |
| SSE keepalive | 独立线程定时器驱动 |
| 配置 JSON 写入 | `extra_init()` 在 HTTP 线程启动前完成，无竞争 |

#### 1.4 测试验证

- 模拟高并发 HTTP 请求时，NTRIP 数据转发不受影响
- SSE 推送延迟不影响基站/用户连接
- HTTP 阻塞操作（远程源表获取）不影响实时数据

---

## 二、节点精细控制

### 目标

通过 Web 界面对各节点进行精细化管理：查看运行配置、热更新参数、启停服务组件。

### 2.1 节点配置查看与热更新

状态：已完成并已联调

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/nodes/config/{id}` | 获取指定节点的运行配置（聚合节点信息 + core/service 配置 + schema） |
| POST | `/api/nodes/action/{id}` | 通过 `action=config_update` 推送配置更新到指定节点 |

#### 实现方案

**配置下发通道**：利用现有 `NODE:{id}` Redis 频道

```
Web HTTP → POST /api/nodes/action/{id}
         → 写入 Redis 配置 (`CONF:CORE` / `CONF:SERVICE`)
         → PUBLISH CASTER:CONF CONFIG
         → PUBLISH NODE:{id} {"type":"action", "action":"config_update", "params":{...}}
节点收到 → Redis_ConfChange_Callback / Redis_NodeChannel_Callback
         → reload_config_from_redis()
```

**可热更新的配置项**：

| 类别 | 配置项 | 说明 |
|------|--------|------|
| 核心 | `Update_Intv` | 状态上报间隔 |
| 核心 | `Key_Expire_Time` | Redis Key 过期时间 |
| 核心 | `Upload_Base_Stat` | 是否上报基站状态 |
| 核心 | `Upload_Rover_Stat` | 是否上报用户状态 |
| 核心 | `Base_Enable_Mult` / `Rover_Enable_Mult` | 允许多连接 |
| 核心 | `Base_Keep_Early` / `Rover_Keep_Early` | 保留早期连接 |
| 核心 | `Notify_Base_Inactive` / `Notify_Rover_Inactive` | 通知离线 |
| 监听 | `Connect_Timeout` | 连接超时时间 |
| 监听 | `Enable_*` 功能开关 | 各类登录开关 |
| 认证 | `Anonymous_Login` | 匿名登录开关 |
| 认证 | `Online_Protection` | 在线保护开关 |

**不可热更新（需重启）**：

| 配置项 | 原因 |
|--------|------|
| Listen_Port | listener 绑定端口 |
| HTTP Port | evhttp 绑定端口 |
| Redis 连接参数 | 需要重建连接 |

### 2.2 服务组件控制

状态：基础能力已完成并已联调，更多控制项待扩展

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| POST | `/api/nodes/action/{id}` | 执行节点操作 |

#### 支持的操作

```json
// 强制刷新集群状态
{ "action": "sync_cluster" }

// 触发日志级别调整
{ "action": "set_log_level", "params": {"level": "debug|info|warn|error"} }

// 热更新配置
{ "action": "config_update", "params": {"section": "core", "key": "update_intv", "value": 5} }
```

其余控制项（暂停监听、批量断开连接、定向断开连接）保留在后续扩展范围内。

#### 实现方案

```
Web HTTP → POST /api/nodes/{id}/action
         → PUBLISH NODE:{id} {"type":"action", "action":"...", "params":{...}}
Node     → Redis_NodeChannel_Callback → 解析 action → 执行
         → HSET NODE:{id}:ACTION_RESULT {"action":"...", "result":"ok|error", "msg":"..."}
Web HTTP → Poll/GET NODE:{id}:ACTION_RESULT 获取执行结果
```

**核心节点处理逻辑扩展**：

```cpp
// caster_internal.cpp — Redis_NodeChannel_Callback
void caster_internal::handle_node_command(const json &cmd) {
    std::string type = cmd.value("type", "");
    if (type == "action") {
        handle_node_action(cmd);
    } else if (type == "config_update") {
        handle_config_update(cmd);
    }
    // ... 原有的 relay broadcast 处理
}
```

### 2.3 节点运行日志查看

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/nodes/{id}/logs` | 获取节点最近日志（Ring Buffer） |

#### 实现方案

- 在 spdlog 中注册一个 **ring_buffer sink**（内存中保留最近 500 条日志）
- HTTP API 读取 ring_buffer 返回
- 支持参数：`level=info&limit=100`

```cpp
// logger.cpp
auto ring_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(500);
// 挂载到 logger
```

---

## 三、Redis 监控与集群状态

### 目标

在 Web 端全面展示 Redis 服务器状态、集群各节点连接健康度、Key 空间分布，帮助运维掌控系统全貌。

### 3.1 Redis INFO 监控

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/monitor/redis` | Redis 完整监控数据 |
| GET | `/api/monitor/redis/keys` | Key 空间分析 |
| GET | `/api/monitor/redis/history` | Redis 指标历史趋势 |

#### 实现方案

**`sync_redis` 新增 `info()` 方法**：

```cpp
std::string info(const char *section = nullptr) {
    auto *reply = redisCommand(_ctx, section ? "INFO %s" : "INFO", section);
    // 解析 INFO 文本为结构化 JSON
}
```

**返回结构化数据**：

```json
{
  "server": {
    "redis_version": "7.2.5",
    "uptime_in_seconds": 86400,
    "tcp_port": 6379
  },
  "clients": {
    "connected_clients": 8,
    "blocked_clients": 0,
    "tracking_clients": 0
  },
  "memory": {
    "used_memory": 5242880,
    "used_memory_human": "5.00M",
    "used_memory_rss": 8388608,
    "used_memory_peak": 6291456,
    "mem_fragmentation_ratio": 1.6
  },
  "stats": {
    "total_connections_received": 1500,
    "total_commands_processed": 500000,
    "instantaneous_ops_per_sec": 120,
    "keyspace_hits": 450000,
    "keyspace_misses": 50000,
    "hit_rate": 0.9
  },
  "replication": {
    "role": "master",
    "connected_slaves": 0
  },
  "keyspace": {
    "db0": { "keys": 150, "expires": 80, "avg_ttl": 15000 }
  }
}
```

### 3.2 Key 空间分析

按业务分类统计 Redis Key：

```json
{
  "categories": [
    { "prefix": "MPT:STAT", "type": "HASH", "count": 1, "fields": 12, "desc": "基站在线状态" },
    { "prefix": "USR:STAT", "type": "HASH", "count": 1, "fields": 85, "desc": "用户在线状态" },
    { "prefix": "LOG:MPT", "type": "HASH", "count": 1, "fields": 2500, "desc": "基站连接日志" },
    { "prefix": "LOG:USR", "type": "HASH", "count": 1, "fields": 15000, "desc": "用户连接日志" },
    { "prefix": "CASTER:NODE", "type": "HASH", "count": 1, "fields": 3, "desc": "集群节点" },
    { "prefix": "NODE:HISTORY:*", "type": "LIST", "count": 3, "total_items": 51840, "desc": "节点历史" },
    { "prefix": "STAT:DAILY:*", "type": "STRING", "count": 30, "desc": "每日统计缓存" },
    { "prefix": "CASTER:MASTER", "type": "STRING", "count": 1, "desc": "Master 锁" }
  ],
  "total_keys": 150,
  "total_memory": "5.2MB"
}
```

实现：`SCAN` 遍历 + `TYPE` + `HLEN`/`LLEN` + 按前缀聚合。

### 3.3 Redis 指标历史记录

在后端定期（60秒）采集 Redis INFO 并存入 Redis LIST：

```
Key: MONITOR:REDIS:HISTORY
Type: LIST
LPUSH: { "ts": ..., "ops": ..., "mem": ..., "clients": ..., "hits": ..., "misses": ... }
LTRIM: 保留最近 1440 条（24小时）
```

### 3.4 集群全局状态仪表盘

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/monitor/cluster` | 集群全局状态汇总 |

返回：

```json
{
  "master_node": "node_01",
  "total_nodes": 3,
  "online_nodes": 3,
  "total_servers": 12,
  "total_clients": 85,
  "total_relays_pull": 5,
  "total_relays_push": 2,
  "cluster_uptime": 864000,
  "total_throughput_in": 52428800,
  "total_throughput_out": 524288000,
  "redis_core": { "status": "ok", "latency_ms": 0.5, "memory": "5.2MB" },
  "redis_auth": { "status": "ok", "latency_ms": 0.3, "memory": "1.1MB" },
  "nodes": [
    {
      "uid": "node_01", "name": "主节点", "is_master": true,
      "cpu": 2.5, "mem": 52428800, "servers": 8, "clients": 45,
      "status": "online", "uptime": 864000
    }
  ]
}
```

### 3.5 SSE 新增监控频道

| 频道名 | 数据源 | 推送频率 |
|--------|--------|----------|
| `redis_monitor` | Redis INFO 解析 | 5 秒 |
| `cluster_status` | 集群汇总 | 5 秒 |

---

## 四、前端界面升级

### 目标

提升 Web 界面的专业程度和交互体验，向生产级监控系统靠拢。

### 4.1 全局布局改版

#### Header 增强

```
┌──────────────────────────────────────────────────────────┐
│ 🧭 NavCaster          集群: 3节点正常  Master: node_01  │
│                        ⬆ 12.5 MB/s  ⬇ 1.2 MB/s         │
│                        🔴 0 告警                   [退出]│
└──────────────────────────────────────────────────────────┘
```

- 顶部状态栏：集群健康摘要（节点数、Master、总吞吐量）
- 告警角标：当有异常状态时显示红点
- 面包屑导航：显示当前位置层级

#### 侧边栏菜单分组

```
📊 监控
  ├── 节点状态 (Dashboard)
  ├── 数据统计
  └── 系统监控 (NEW)

🔌 连接
  ├── 基准站
  ├── 移动站
  └── 连接历史

⚙️ 配置
  ├── 源列表
  ├── 挂载点别名
  ├── 访问管理
  └── 账号管理

🔄 数据转发
  ├── 数据接入 (Pull)
  ├── 数据推送 (Push)
  └── 源表视图

🛠️ 系统
  ├── 系统设置
  └── 操作日志 (NEW)
```

### 4.2 组件风格统一

#### 通用页面模板

```tsx
<PageContainer
  title="基准站"
  subtitle="在线基站连接状态"
  extra={<StatusBadge online={12} total={15} />}
  breadcrumb={[{ title: '连接' }, { title: '基准站' }]}
>
  {/* 页面内容 */}
</PageContainer>
```

创建共享组件：

| 组件 | 用途 |
|------|------|
| `PageContainer` | 统一页面包裹器（标题 + 副标题 + 操作区 + 面包屑） |
| `StatusBadge` | 在线/总数状态徽章 |
| `MetricCard` | 统一指标卡片（数值 + 趋势 + 图标） |
| `MiniChart` | 内联迷你趋势图（sparkline） |
| `DataTable` | 增强表格（搜索 + 筛选 + 列显隐 + 导出） |
| `ConfirmAction` | 危险操作确认弹窗 |
| `NodeSelector` | 节点选择下拉框（跨节点操作） |
| `TimeRangeSelector` | 统一时间范围选择器 |

#### 表格增强

- 搜索框：支持模糊搜索挂载点名、用户名、IP
- 列筛选：可勾选显示/隐藏列
- 列排序：点击表头排序
- 导出：CSV/JSON 导出按钮
- 批量操作：多选 + 批量断开/操作

#### 响应式设计

- 小屏幕侧边栏自动折叠
- 表格在小屏幕下切换为卡片视图
- 弹窗适配移动端

### 4.3 Dashboard 改版

将 Dashboard 从"节点卡片列表"升级为"集群总览仪表盘"：

```
┌───────────────────────────────────────────────────┐
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐    │
│ │节点 3 │ │基站12│ │用户85│ │吞吐量│ │Redis │    │
│ │全部在线│ │ +2↑  │ │ -3↓  │ │52MB/s│ │正常  │    │
│ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘    │
│                                                   │
│ ┌─────────────────────┐ ┌──────────────────────┐  │
│ │ 实时吞吐量趋势       │ │ 节点负载对比          │  │
│ │ [Area Chart 5min]   │ │ [Bar Chart per-node] │  │
│ └─────────────────────┘ └──────────────────────┘  │
│                                                   │
│ ┌── 节点卡片 ─────────────────────────────────┐   │
│ │ [node_01 ★Master]  [node_02]  [node_03]    │   │
│ │  CPU: 2.5%          CPU: 5.1%  CPU: 3.2%   │   │
│ │  MEM: 50MB          MEM: 48MB  MEM: 51MB   │   │
│ │  MPT: 8  USR: 45   MPT: 4     MPT: 0      │   │
│ └─────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────┘
```

新增内容：
- 顶部指标卡片行：全局汇总数字（含变化趋势箭头）
- 实时吞吐量趋势图（最近 5 分钟，Area Chart）
- 节点负载对比柱状图
- Redis 状态指示卡片
- 节点卡片增加点击展开详情动作

### 4.4 暗色主题精调

| 元素 | 当前 | 优化 |
|------|------|------|
| 卡片阴影 | 无 | 添加微弱 box-shadow |
| 表格行交替色 | 无 | 添加交替行背景色 |
| 状态颜色 | 单一绿/红 | 增加黄/橙/蓝多级状态色 |
| 过渡动画 | 无 | 页面切换淡入淡出 |
| 加载状态 | Spin | 骨架屏 Skeleton |
| 空状态 | Ant Design Empty | 自定义插图 |

---

## 五、详细信息页面增强

### 目标

为各实体（基站、用户、节点、Relay）提供更丰富的详情页，帮助运维快速定位问题。

### 5.1 基站详情页增强 (`/servers/:id`)

当前：基本连接信息 + 历史连接表

增强为多 Tab 页：

```
┌── 概览 ──┬── 数据流 ──┬── 订阅者 ──┬── 历史 ──┐
│                                                │
│ ┌ 基本信息 ────────┐ ┌ 实时指标 ──────────┐   │
│ │ 挂载点: RTCM3_GPS│ │ 接收速率: 1.2 KB/s │   │
│ │ 账户: admin      │ │ 总接收: 52.3 MB    │   │
│ │ IP: 192.168.1.1  │ │ 在线: 3h 25m       │   │
│ │ 连接类型: SERVER  │ │ 数据格式: RTCM 3.3 │   │
│ │ 节点: node_01    │ │ 消息类型: 1004,1012 │   │
│ └──────────────────┘ └────────────────────┘   │
│                                                │
│ ┌ 订阅者列表 (实时) ────────────────────────┐  │
│ │ user_01  192.168.1.50  RTCM3_GPS  5min   │  │
│ │ user_02  10.0.0.15     RTCM3_GPS  2h     │  │
│ └──────────────────────────────────────────┘  │
│                                                │
│ ┌ 数据速率趋势 (最近1小时) ─────────────────┐  │
│ │ [Line Chart]                              │  │
│ └───────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

#### 新增 Tab 页

| Tab | 数据来源 | 内容 |
|-----|----------|------|
| 概览 | MPT:STAT SSE | 基本信息 + 实时指标 |
| 数据流 | STR:STAT SSE | RTCM/NMEA 消息类型分布、数据速率趋势 |
| 订阅者 | USR:STAT SSE(筛选) | 正在使用此基站的用户列表 |
| 历史 | LOG:MPT API | 历史连接记录 |

#### 后端 API 新增

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/servers/{key}/subscribers` | 获取指定基站的当前订阅者列表 |

### 5.2 用户详情页增强 (`/clients/:id`)

增强为多 Tab 页：

| Tab | 内容 |
|-----|------|
| 概览 | 连接信息 + 实时指标（接收/发送速率、NMEA 频率） |
| 数据流 | 正在接收的数据格式、差分数据源 |
| 位置 | NMEA 解码的经纬度（如有） + 地图标注 |
| 历史 | 历史连接记录 |

### 5.3 节点详情页增强 (`/nodes/:id`)

当前：历史趋势图

增强为：

| Tab | 内容 |
|-----|------|
| 概览 | 节点信息 + 实时 CPU/内存/网络指标 |
| 趋势 | CPU/内存/连接数/吞吐量 24h 趋势图（已有） |
| 连接 | 该节点上的所有基站和用户列表（筛选 node_id） |
| 配置 | 节点运行配置查看 + 热更新表单 |
| 控制 | 操作面板（暂停/恢复/断开/日志级别） |
| 日志 | 最近日志查看器（Ring Buffer） |

### 5.4 Relay 详情页（新增）

当前 Pull/Push 列表直接在列表页操作，没有详情页。

新增 `/relay/pull/:id` 和 `/relay/push/:id`：

| 区块 | 内容 |
|------|------|
| 配置信息 | 远程地址、挂载点、账号、NTRIP 版本 |
| 运行状态 | 连接状态、运行时长、数据速率、重连次数 |
| 数据统计 | 总接收/发送字节、消息计数 |
| 操作 | 启动/停止/编辑/删除 按钮 |
| 日志 | Relay 连接日志摘要 |

### 5.5 系统监控页（新增 `/monitor`）

状态：已完成并已联调

当前页面已实现以下内容：

- 集群概览卡片与节点状态列表
- Redis 服务器、客户端、内存、统计信息展示
- Key 空间分类分析表
- 10 秒自动刷新

以下内容顺延到后续与 `3.3 Redis 历史趋势` 一起补充：

- Redis QPS / 内存历史趋势图
- 更长时间跨度的时序分析

原始设计目标如下：

```
┌─────────────────────────────────────────────────┐
│ Redis 核心状态                                   │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  │
│ │版本  │ │内存  │ │连接数│ │QPS   │ │命中率│  │
│ │7.2.5 │ │5.2MB │ │  8   │ │ 120  │ │ 90%  │  │
│ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘  │
│                                                 │
│ ┌ QPS 趋势 (24h) ──────────────────────────┐   │
│ │ [Area Chart]                              │   │
│ └───────────────────────────────────────────┘   │
│                                                 │
│ ┌ 内存趋势 (24h) ──────────────────────────┐   │
│ │ [Area Chart]                              │   │
│ └───────────────────────────────────────────┘   │
│                                                 │
│ ┌ Key 空间分析 ────────────────────────────┐    │
│ │ 前缀           类型   数量  字段数  大小  │    │
│ │ MPT:STAT       HASH    1    12    2.1KB  │    │
│ │ USR:STAT       HASH    1    85   15.3KB  │    │
│ │ LOG:MPT        HASH    1   2500  480KB   │    │
│ │ LOG:USR        HASH    1  15000  2.8MB   │    │
│ │ ...                                      │    │
│ └──────────────────────────────────────────┘    │
│                                                 │
│ ┌ 双 Redis 状态 ──────────────────────────┐     │
│ │ Caster Redis: ✅ 正常  延迟: 0.5ms      │     │
│ │ Auth Redis:   ✅ 正常  延迟: 0.3ms      │     │
│ └─────────────────────────────────────────┘     │
└─────────────────────────────────────────────────┘
```

---

## 六、系统设置完善

### 目标

将当前仅有"密码修改"的设置页扩展为完整的系统配置管理。

### 6.1 设置页面分区

```
┌── 服务配置 ──┬── 核心配置 ──┬── 认证配置 ──┬── 安全 ──┐
│                                                        │
│ 服务配置 (CONF:SERVICE)                                │
│ ┌─────────────────────────────────────────────┐       │
│ │ 监听端口: [2101]          ⚠️ 需重启生效       │       │
│ │ 连接超时: [30] 秒                             │       │
│ │ 基站心跳间隔: [30] 秒                         │       │
│ │ 基站心跳消息: [_____]                         │       │
│ │                                               │       │
│ │ 功能开关:                                     │       │
│ │ [✓] Source 登录  [✓] Server 登录              │       │
│ │ [✓] Client 登录  [✓] Nearest 登录             │       │
│ │ [✓] 代理协议     [✓] 别名功能                 │       │
│ │ [✓] 网格登录     [ ] 无CRLF Header            │       │
│ │                                               │       │
│ │ HTTP API:                                     │       │
│ │ 端口: [8080]              ⚠️ 需重启生效       │       │
│ │ CORS Origin: [*]                              │       │
│ │ Web 根目录: [/opt/navcaster/web]              │       │
│ │                             [保存] [重置]     │       │
│ └─────────────────────────────────────────────┘       │
└────────────────────────────────────────────────────────┘
```

### 6.2 配置 API 增强

当前 `/api/config/*` 已支持 GET/PUT，但需要增强：

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/config/schema` | 返回配置项的元数据（类型、范围、是否需重启） |
| POST | `/api/config/validate` | 验证配置值是否合法（不写入） |
| POST | `/api/config/apply` | 应用配置并通知所有节点刷新 |

**配置元数据 Schema 示例**：

```json
{
  "service.listener.listen_port": {
    "type": "number", "min": 1, "max": 65535,
    "default": 2101, "restart_required": true,
    "label": "监听端口", "group": "service"
  },
  "service.listener.connect_timeout": {
    "type": "number", "min": 5, "max": 300,
    "default": 30, "restart_required": false,
    "label": "连接超时(秒)", "group": "service"
  },
  "core.base_enable_mult": {
    "type": "boolean", "default": false,
    "restart_required": false,
    "label": "基站允许多连接", "group": "core"
  }
}
```

### 6.3 配置变更广播

配置保存后通过 `PUBLISH CASTER:CONF CONFIG` 通知所有节点重新加载 Redis 中的配置：

```cpp
// http_handler.cpp — handle_update_config
redis.set(key, body.dump());
redis.publish("CASTER:CONF", "CONFIG");  // 通知所有节点

// caster_internal.cpp — Redis_ConfChange_Callback
if (topic == "CONFIG") {
    reload_config_from_redis();
}
```

---

## 七、操作日志与安全增强

### 目标

记录所有管理操作，提供审计追溯能力。

### 7.1 操作日志

状态：基础能力已完成并已联调

#### 数据结构

```
Key: LOG:AUDIT
Type: LIST (LPUSH + LTRIM ~5000)
```

```json
{
  "ts": 1700000000,
  "user": "admin",
  "action": "update_alias",
  "target": "RTCM3_VRS",
  "detail": {"old_target": "BASE_01", "new_target": "BASE_02"},
  "ip": "192.168.1.100",
  "result": "ok"
}
```

#### 记录的操作

| 操作类别 | 具体操作 |
|----------|----------|
| 节点控制 | `sync_cluster` / `set_log_level` / `config_update`（已实现） |
| 其他管理操作 | 账号、源列表、别名、访问控制、Relay、认证事件（待补齐） |

#### 后端 API

| Method | Path | 说明 |
|--------|------|------|
| GET | `/api/logs/audit` | 操作日志列表（支持分页、筛选） |

#### 实现方式

当前实现为在 `http_handler` 中添加 `audit_log()` 辅助函数，并将日志写入 Redis LIST：

```cpp
void http_handler::audit_log(const HttpRequest &req,
                              const std::string &action,
                              const std::string &target,
                              const json &detail) {
    json entry;
    entry["ts"] = time(nullptr);
    entry["user"] = ...;
    entry["action"] = action;
    entry["target"] = target;
    entry["detail"] = detail;
    entry["ip"] = ...;
    entry["result"] = "ok";
    sync_redis::instance().lpush("LOG:AUDIT", entry.dump());
    sync_redis::instance().ltrim("LOG:AUDIT", 0, 4999);
}
```

### 7.2 Web 操作日志页面（新增 `/audit`）

状态：已完成基础版

| 列 | 说明 |
|----|------|
| 时间 | 操作时间 |
| 操作人 | 登录用户名 |
| 操作 | 操作类型（中文标签 + 颜色 Tag） |
| 目标 | 操作对象标识 |
| 详情 | JSON 展开 |
| IP | 客户端 IP |
| 结果 | 成功/失败 |

当前已支持按操作类型、用户筛选和分页浏览；时间范围筛选可在后续补充。

### 7.3 安全增强

| 增强项 | 说明 |
|--------|------|
| Token 过期 | JWT 或带 TTL 的 token，自动过期需重新登录 |
| 请求频率限制 | 基于 IP 的 rate limiting（防暴力破解） |
| 密码强度 | 前端 + 后端密码复杂度校验 |
| 操作确认 | 危险操作（删除、断开连接）二次确认弹窗 |

---

## 优先级与实施顺序

### Phase 1: 基础架构（P0，建议先做）

| 任务 | 预期复杂度 | 说明 |
|------|------------|------|
| 1.1 HTTP 线程分离 | 高 | 核心架构变更，后续功能的基础 |
| 〇 节点历史分级存储 | 中 | 5s/60s/5min 三级聚合，覆盖 1 年 |
| 4.2 通用组件库 | 中 | PageContainer / MetricCard / DataTable |
| 4.1 布局改版 | 中 | Header 增强 + 菜单分组 |

### Phase 2: 监控与控制（P1）

| 任务 | 预期复杂度 | 说明 | 状态 |
|------|------------|------|------|
| 3.1 Redis INFO API | 中 | `sync_redis::info()` + 解析 | ✅ 已完成 |
| 3.2 Key 空间分析 | 中 | SCAN + 聚合 | ✅ 已完成 |
| 3.4 集群状态 API | 低 | 汇总现有数据 | ✅ 已完成 |
| 5.5 系统监控页面 | 高 | 全新页面，Redis + 集群全览 | ✅ 已完成 |
| 2.1 节点配置查看 | 中 | 读取 Redis 配置 | ✅ 已完成 |
| 2.2 服务组件控制 | 高 | NODE 频道命令扩展 | ✅ 基础能力完成 |

### Phase 3: 页面增强（P1-P2）

| 任务 | 预期复杂度 | 说明 |
|------|------------|------|
| 5.1 基站详情增强 | 中 | 多 Tab + 订阅者列表 |
| 5.2 用户详情增强 | 中 | 多 Tab + 数据流 |
| 5.3 节点详情增强 | 高 | 配置 + 控制 + 日志 Tab |
| 5.4 Relay 详情页 | 中 | 新增路由和页面 |
| 4.3 Dashboard 改版 | 高 | 图表 + 汇总 + 集群总览 |

### Phase 4: 系统完善（P2）

| 任务 | 预期复杂度 | 说明 | 状态 |
|------|------------|------|------|
| 6.1-6.3 设置页完善 | 中 | 配置 Schema + 表单 | ⏳ 未开始 |
| 7.1-7.2 操作日志 | 中 | 审计日志记录 + 页面 | ✅ 基础能力完成 |
| 7.3 安全增强 | 中 | Token 过期 + 频率限制 | ⏳ 未开始 |
| 3.3 Redis 历史趋势 | 低 | 定时采集 + 存储 | ⏳ 未开始 |
| 2.3 节点日志查看 | 中 | Ring Buffer Sink | ⏳ 未开始 |

---

## 技术要点备忘

### 线程分离核心代码路径

```
ntrip_caster::extra_init()
  → event_base_new()            // 创建 HTTP event_base
  → http_handler::init(http_base, ...)
    → http_server::init(http_base, port, bind_addr)
    → sse_manager::init(http_base, 2)
  → std::thread(event_base_dispatch, http_base)  // 启动 HTTP 线程
```

### sync_redis 需新增方法

| 方法 | 用途 |
|------|------|
| `info(section)` | Redis INFO 命令 |
| `scan(cursor, pattern, count)` | SCAN 遍历 Key |
| `type(key)` | TYPE 命令 |
| `hlen(key)` | HASH 字段数 |
| `llen(key)` | LIST 长度 |
| `lpush(key, value)` | LIST 头部插入 |
| `ltrim(key, start, stop)` | LIST 裁剪 |
| `dbsize()` | KEY 总数 |
| `memory_usage(key)` | 单 Key 内存占用 |
| `ping()` | 延迟检测 |

### 前端新增依赖（建议）

| 包 | 用途 |
|----|------|
| `@ant-design/pro-components` | ProTable / ProForm / PageContainer（可选） |
| `lodash-es` | 节流、防抖、深比较 |
| `dayjs` | 已有，继续使用 |

### Redis 新增 Key

| Key | 类型 | 说明 |
|-----|------|------|
| `NODE:HISTORY:{id}` | LIST | 节点历史 RAW（5s，最近 120,960 条 = 7 天） |
| `NODE:HISTORY:{id}:1M` | LIST | 节点历史 1MIN 聚合（60s，最近 33,120 条 = 23 天） |
| `NODE:HISTORY:{id}:5M` | LIST | 节点历史 5MIN 聚合（5min，最近 96,480 条 = 335 天） |
| `MONITOR:REDIS:HISTORY` | LIST | Redis 指标历史（最近 1440 条） |
| `LOG:AUDIT` | LIST | 操作审计日志（最近 5000 条） |
| `NODE:{id}:ACTION_RESULT` | STRING(TTL 60s) | 节点操作执行结果 |
