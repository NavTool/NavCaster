# NavCaster 第一期迭代开发方案

> ✅ **已完成** — 本期开发于 2026-04 完成。后续开发请参见 [第二期迭代方案](development-plan-v2.md)。

---

## 完成状态

| 模块 | 状态 |
|------|------|
| 一、上下线记录与历史追溯 | ✅ 已完成（LOG:MPT/LOG:USR HASH + 连接历史页面） |
| 二、崩溃恢复与数据一致性 | ✅ 已完成（flush_online_history + cleanup_stale_history） |
| 三、节点历史状态统计 | ✅ 已完成（NODE:HISTORY LIST + 趋势图） |
| 四、基站/用户历史分析功能 | ✅ 已完成（统计概览 + 排行 + 趋势 + 个体历史 + 每日缓存） |
| 五、多节点数据同步完善 | ✅ 核心完成（Master 快速切换 + 状态跟踪 + 配置即时同步） |
| 六、异常处理与排查日志优化 | ✅ 部分完成（结构化日志、关键路径增强） |
| 七、架构优化（中长期） | ⏳ 移至第二期 |

---

## 目录

- [一、上下线记录与历史追溯](#一上下线记录与历史追溯)
- [二、崩溃恢复与数据一致性](#二崩溃恢复与数据一致性)
- [三、节点历史状态统计](#三节点历史状态统计)
- [四、基站/用户历史分析功能](#四基站用户历史分析功能)
- [五、多节点数据同步完善](#五多节点数据同步完善)
- [六、异常处理与排查日志优化](#六异常处理与排查日志优化)
- [七、架构优化（中长期）](#七架构优化中长期)

---

## 一、上下线记录与历史追溯

### 目标

记录每次基站和用户的连接/断开事件，支持历史查询。

### 当前问题

- 基站/用户状态仅存在于 `MPT:STAT` / `USR:STAT`（HSETEX，自动过期）
- 断开连接后状态消失，无法追溯历史

### 方案设计

#### 1.1 Redis 事件日志（短期存储）

新增 Redis Key：

| Key | 类型 | 说明 |
|-----|------|------|
| `LOG:MPT:CONNECT` | STREAM | 基站连接事件流 |
| `LOG:MPT:DISCONNECT` | STREAM | 基站断开事件流 |
| `LOG:USR:CONNECT` | STREAM | 用户连接事件流 |
| `LOG:USR:DISCONNECT` | STREAM | 用户断开事件流 |

使用 Redis Stream（`XADD`）记录事件，自动 ID 含时间戳：

```redis
XADD LOG:MPT:CONNECT * mount RTCM3_GPS node node_01 host 192.168.1.100 port 5201
XADD LOG:MPT:DISCONNECT * mount RTCM3_GPS node node_01 reason timeout duration 3600
```

设置 `MAXLEN ~10000` 限制内存占用。

#### 1.2 实现步骤

1. **CasterCore 修改**：
   - `Register_Record()` 中增加 `XADD LOG:*:CONNECT`
   - `Withdraw_Record()` 中增加 `XADD LOG:*:DISCONNECT`（含 duration 计算）
   - 使用异步 Redis（已有的 `_pub_context`），不阻塞

2. **HTTP API 新增**：
   - `GET /api/logs/connections` — 查询连接历史（支持时间范围、类型筛选）
   - `GET /api/logs/connections/count` — 按时间段统计连接次数

3. **Web 前端**：
   - 新增"连接历史"页面
   - 支持按挂载点/用户/时间范围筛选
   - 展示时间线视图

#### 1.3 数据字段

连接事件：
```json
{
  "uid": "node_01:RTCM3_GPS",
  "mountpoint": "RTCM3_GPS",
  "node_id": "node_01",
  "host": "192.168.1.100",
  "port": 5201,
  "username": "",
  "connect_type": "SERVER",
  "timestamp": 1700000000
}
```

断开事件：
```json
{
  "uid": "node_01:RTCM3_GPS",
  "mountpoint": "RTCM3_GPS",
  "node_id": "node_01",
  "reason": "timeout|normal|error",
  "duration_seconds": 3600,
  "bytes_total": 1048576,
  "timestamp": 1700003600
}
```

---

## 二、崩溃恢复与数据一致性

### 目标

确保程序异常退出后，上下线记录和系统状态能正确恢复。

### 当前机制

- `CASTER:MASTER`：NX + EX，30 秒自动过期 → ✅ 无残留
- `CASTER:NODE`：HSETEX 自动过期 → ✅ 无残留
- `MPT:STAT` / `USR:STAT`：HSETEX 自动过期 → ✅ 无残留
- `MPT:LIST` / `USR:LIST`：**无 TTL**，程序崩溃后可能残留 → ⚠️ 需处理
- `MPT:REC:*` / `USR:REC:*`：**无 TTL** → ⚠️ 需处理

### 方案设计

#### 2.1 状态数据清理

**方案 A：启动时清理（推荐）**

节点启动时执行：
```
1. 扫描 MPT:LIST，删除属于本节点的残留条目
2. 扫描 USR:LIST，删除属于本节点的残留条目  
3. 删除 MPT:REC:<本节点前缀>:*
4. 删除 USR:REC:<本节点前缀>:*
```

**方案 B：给连接列表也加 TTL**

为 `MPT:LIST` / `USR:LIST` 的 Hash field 添加 HEXPIRE，与状态数据同步续期。

#### 2.2 崩溃断开日志补偿

上下线日志的补偿逻辑：

```
节点启动时:
    1. 读取本节点上次记录的在线列表（可存储在本地文件）
    2. 对比当前 Redis 中的在线状态
    3. 对于"上次在线但现在不在线"的条目，补写 DISCONNECT 日志
       reason = "crash_recovery"
```

#### 2.3 本地状态快照

定期将当前在线列表写入本地文件（如 `status_snapshot.json`）：
- 用于崩溃后的日志补偿
- 间隔 30 秒写一次
- 仅在有变化时写入

#### 2.4 信号处理增强

```cpp
// 已有的信号处理增强
signal(SIGTERM, graceful_shutdown);
signal(SIGINT, graceful_shutdown);

graceful_shutdown() {
    // 1. 停止接受新连接
    // 2. 为所有在线会话写入 DISCONNECT 日志 (reason="shutdown")
    // 3. 清理 Redis 中本节点的数据
    // 4. event_base_loopbreak()
}
```

---

## 三、节点历史状态统计

### 目标

记录各节点的历史运行状态（CPU、内存、连接数等），支持趋势分析。

### 方案设计

#### 3.1 Redis TimeSeries（如可用）

```redis
TS.ADD node:{node_id}:cpu * <cpu_percent>
TS.ADD node:{node_id}:memory * <memory_mb>
TS.ADD node:{node_id}:connections * <count>
```

保留策略：原始数据 24 小时，1 分钟聚合保留 7 天，1 小时聚合保留 90 天。

#### 3.2 Redis Stream 替代方案

若 Redis 版本不支持 TimeSeries：

```redis
XADD NODE:HISTORY:{node_id} MAXLEN ~86400 * cpu 2.5 mem 50 conn 150 mpt 10 usr 80
```

每分钟记录一次，保留约 60 天数据。

#### 3.3 HTTP API

- `GET /api/stats/nodes/{node_id}/history` — 节点历史指标
  - 参数：`start`, `end`, `interval` (1m/5m/1h/1d)
- `GET /api/stats/nodes/summary` — 全部节点汇总

#### 3.4 实现步骤

1. 在 `heart_beat` 或 `info_upload` 模块中增加历史记录写入
2. 新增 HTTP handler 处理历史查询
3. Web 前端增加图表展示（推荐 ECharts）

---

## 四、基站/用户历史分析功能

> **实现状态**: ✅ 已完成

### 目标

提供基站和用户的历史连接记录、数据统计、趋势分析。

### 已实现功能

#### 4.1 数据汇总统计 ✅

- `GET /api/stats/overview?start=&end=&date=` — 汇总统计（连接数、峰值并发、平均时长、唯一基站/用户数、每小时趋势）
- `GET /api/stats/daily/{YYYY-MM-DD}` — 指定日期统计，历史日期自动缓存到 `STAT:DAILY:{date}`（TTL 7天），今日数据实时计算

#### 4.2 排行榜 ✅

- `GET /api/stats/mountpoints/ranking?start=&end=&limit=20` — 基站 TOP 排行（在线时长、连接次数、最后活跃）
- `GET /api/stats/users/ranking?start=&end=&limit=20` — 用户 TOP 排行（使用时长、连接次数、使用基站数）

#### 4.3 个体历史 ✅

- `GET /api/stats/mountpoints/history/{mount}` — 指定基站的所有历史连接记录
- `GET /api/stats/users/history/{user}` — 指定用户的所有历史连接记录
- 每条记录包含：连接时间、断开时间、时长、节点、账户、类型、在线状态

#### 4.4 Web 前端 ✅

- **数据统计页面** (`/statistics`)：
  - 时间范围选择器（今日/7天/30天/自定义日期）
  - 8 个概览统计卡片
  - 每小时趋势柱状图（recharts BarChart）
  - 基站/用户 TOP 20 排行表
  - 自定义历史日期使用缓存 API（`getStatsDaily`）
- **基站详情页**：历史连接表格（ConnectionHistoryTable 共享组件）
- **用户详情页**：历史连接表格
- **账户详情页**：历史登录表格

---

## 五、多节点数据同步完善

> **实现状态**: ✅ 已完成核心功能

### 当前机制

- `CASTER:BROADCAST` 频道广播状态变更
- Master 通过 `NODE:<id>` 频道下发 Relay 任务
- 各节点独立上报 `MPT:STAT` / `USR:STAT` / `CASTER:NODE`

### 已知问题

1. **Master 切换延迟**：~~Master 宕机后需等待 30 秒 TTL 过期~~ ✅ 已缩短至 15 秒
2. **广播消息丢失**：Redis Pub/Sub 不保证投递，节点重启时可能错过消息
3. **Relay 任务重复**：Master 切换时可能导致任务重复下发
4. **别名/访问控制同步**：~~变更通过广播通知，但节点可能未收到~~ ✅ 已实现 CASTER:CONF 订阅

### 已实现功能

#### 5.1 快速 Master 切换 ✅

- `CASTER:MASTER` TTL 从 30 秒缩短到 15 秒（`_master_expire_time`）
- 新增 `_is_master` / `_current_master_id` 状态跟踪
- `Redis_KeepMaster_Callback` 检查续期是否成功，仅成功时调用 `sync_cluster_state()`
- Master 身份变更时输出 spdlog::info 日志
- `/api/status` 返回 `master_node` 字段
- Dashboard 节点卡片显示 Master 金色徽章（CrownOutlined + Tag）

#### 5.4 配置变更即时同步 ✅

- HTTP 层别名 CRUD 操作后发布 `PUBLISH CASTER:CONF ALIAS`
- 各节点 `init_sub_context()` 订阅 `CASTER:CONF` 频道
- `Redis_ConfChange_Callback` 收到 ALIAS 消息后立即调用 `download_alias_rule()`
- 原有 1 秒轮询作为兜底保留

### 待优化（低优先级）

#### 5.2 消息可靠投递

将关键操作广播从 Pub/Sub 改为 Redis Stream：

```redis
XADD CASTER:EVENTS * type mpt_online mount RTCM3_GPS node node_01
```

节点重启时从上次消费位置继续读取（Consumer Group）。

#### 5.3 Relay 任务幂等性

- 每个 Relay 任务增加 `generation` 版本号
- 节点执行前检查 Record 中的版本号是否匹配
- 任务状态中记录当前执行的版本号

---

## 六、异常处理与排查日志优化

### 当前问题

- 部分错误路径缺少日志
- 异常情况下缺乏诊断信息
- Redis 连接断开时的恢复逻辑不够健壮

### 优化方案

#### 6.1 分级日志策略

| 级别 | 使用场景 | 示例 |
|------|----------|------|
| ERROR | 影响核心功能的错误 | Redis 连接失败、会话创建异常 |
| WARN | 可恢复的异常状态 | 客户端连接超时、数据解码失败 |
| INFO | 关键业务事件 | 基站上/下线、用户连接/断开、Relay 启停 |
| DEBUG | 详细调试信息 | 每帧数据处理、Redis 命令详情 |

#### 6.2 结构化日志

```cpp
spdlog::info("[MPT:CONNECT] mount={} node={} host={} port={}", 
             mount, node_id, host, port);
spdlog::warn("[REDIS:RECONNECT] instance={} attempt={} delay={}ms",
             instance_name, attempt, delay_ms);
spdlog::error("[SESSION:ERROR] uid={} type={} error={} errno={}",
              uid, type_name, error_msg, errno);
```

#### 6.3 需要增强日志的关键位置

| 位置 | 增加内容 |
|------|----------|
| `ntrip_listener` 连接接受 | 客户端 IP、请求方法、挂载点、User-Agent |
| `process_request` 会话创建 | 会话类型、分配的 UID |
| `carrier_base` 会话结束 | 断开原因、在线时长、收发字节数 |
| `caster_internal` Redis 操作 | 失败的 Redis 命令、重连尝试 |
| `sync_redis` 连接管理 | 连接/断开/重连事件 |
| `http_handler` 请求处理 | 请求路径、耗时、错误详情 |
| `sse_manager` 轮询 | 客户端数、频道数据量、耗时 |
| Relay 任务 | 连接状态变化、数据同步统计 |

#### 6.4 异常处理增强

1. **Redis 连接恢复**：
   - `sync_redis`：增加指数退避重连（当前是每次请求时检查）
   - 异步 Redis：增加 `redisAsyncSetDisconnectCallback` 自动重连

2. **会话异常处理**：
   - bufferevent 错误回调中记录详细错误信息
   - 添加会话超时检测（长时间无数据的基站/用户）

3. **内存监控**：
   - 定期记录当前会话数、缓冲区使用量
   - 设置连接数上限告警

4. **HTTP 请求保护**：
   - 为所有 handler 添加 try-catch（避免异常导致 evhttp 回调崩溃）
   - 添加请求频率限制

---

## 七、架构优化（中长期）

### 7.1 HTTP 服务独立线程化

**目标**：将 HTTP API 和 SSE 从主事件循环分离

```
主线程 (event_base_1):
    ├── NTRIP Listener
    ├── CasterCore Redis (async)
    └── Process Queue

HTTP 线程 (event_base_2):
    ├── evhttp
    ├── SSE Manager
    └── sync_redis (或改为 async)
```

**关键挑战**：
- `CASTER::` 接口需要加锁或改为线程安全
- 或通过 Redis 完全解耦（HTTP 线程只访问 Redis，不直接调用 Core）

### 7.2 SSE 改为 Redis Keyspace Notification

替代当前的轮询模式：
```
Redis CONFIG SET notify-keyspace-events Kh
SUBSCRIBE __keyevent@0__:hset
```

收到通知后按需拉取变更的 key，避免全量 HGETALL。

### 7.3 外部数据存储

当历史数据量增长后，考虑引入：
- **SQLite**：适合单节点部署，存储上下线历史、统计数据
- **InfluxDB / TimescaleDB**：适合时序指标存储
- **PostgreSQL**：适合多节点共享的结构化数据

### 7.4 异步 handle_fetch_sourcetable

将远程源表获取改为非阻塞：
- 使用 `bufferevent_socket_connect_hostname()` 异步连接
- 或创建独立线程处理远程 TCP 请求
- 前端通过轮询或 WebSocket 获取结果

---

## 优先级排序

| 优先级 | 任务 | 预期复杂度 | 依赖关系 |
|--------|------|------------|----------|
| P0 | 异常处理与日志优化（六） | 中 | 无 |
| P0 | 崩溃恢复（二） | 中 | 无 |
| P1 | 上下线记录（一） | 中 | 无 |
| P1 | 节点历史统计（三） | 中 | 无 |
| P2 | 历史分析功能（四） | 高 | 依赖一、三 |
| P2 | 多节点同步完善（五） | 高 | 无 |
| P3 | 架构优化（七） | 高 | 建议在其他功能稳定后 |
