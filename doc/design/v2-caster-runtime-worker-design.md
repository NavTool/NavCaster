# NavCaster v2 Caster Runtime / Worker Design

更新时间：2026-06-27
任务：NC-081 v2 Architecture API Data Contract
状态：v2 冻结契约
适用范围：navcaster-caster C++ Runtime、Acceptor handoff、Worker session/bufferevent/map 所有权、mount ownership、draining 和数据面规则。
可信度：架构冻结文档；后续 Caster Runtime MVP 和 Reviewer 所有权审查必须按本文执行。

## 1. 设计结论

v2 Caster Runtime 从第一版开始按单进程多 Worker 架构设计。

核心结论：

```text
navcaster-caster 是 C++ 实时数据面进程。
一个 Runtime 对外监听一个或多个明确配置的 NTRIP 端口，第一版建议一个主端口。
Runtime 内有 Acceptor、WorkerManager、RuntimeManager 和多个 CasterWorker。
每个 Worker 拥有独立 event_base。
每个 Worker 独占自己的 session、bufferevent、mount map、状态 map 和 Redis async context。
Acceptor 只做轻量 header 解析和 fd handoff，不承载正式 session。
mount ownership 优先于 round-robin，source 和 client 尽量落到同一个 owner Worker。
Caster 热路径不访问 PostgreSQL。
```

## 2. Runtime 内部结构

```text
navcaster-caster
  RuntimeManager
    - load config
    - runtime lifecycle
    - health / metrics
    - local control API

  Acceptor
    - listen socket
    - accept connection
    - read NTRIP header
    - parse ConnectInfo
    - choose Worker
    - handoff fd + initial bytes

  WorkerManager
    - create / stop workers
    - mount owner registry
    - worker_count adjustment
    - worker draining
    - runtime metrics aggregation

  CasterWorker-0..N
    - event_base
    - WorkerCore
    - session containers
    - local fan-out
    - Redis pub/sub
    - auth/config cache
```

目标目录：

```text
caster/
  app/
  runtime/
  transport/
  worker/
  session/
  domain/
  storage/redis/
  infra/
```

推荐 namespace：

```text
navcaster::caster::app
navcaster::caster::runtime
navcaster::caster::transport
navcaster::caster::worker
navcaster::caster::session
navcaster::caster::domain
navcaster::caster::storage::redis
navcaster::caster::infra
```

## 3. Acceptor Handoff

Acceptor 流程：

```text
1. accept socket。
2. 建立临时读状态，只读取 NTRIP/HTTP header 和必要初始 bytes。
3. 解析 connect_type、mount、auth header、Ntrip-GGA、remote address。
4. 查询或创建 mount owner。
5. 将 fd、ConnectInfo、initial_bytes 发送给目标 Worker mailbox。
6. handoff 成功后 Acceptor 不再访问 fd。
```

HandoffMessage：

```cpp
struct HandoffMessage {
    evutil_socket_t fd;
    ConnectInfo connect_info;
    std::string initial_bytes;
    std::string remote_addr;
    uint16_t remote_port;
    uint64_t accepted_at_ms;
};
```

生命周期规则：

```text
Acceptor 临时 bufferevent 释放时不得关闭 fd。
正式 bufferevent 只能在目标 Worker 的 event_base 上创建。
initial_bytes 必须交给 Worker session parser 继续消费。
handoff 成功后 fd 所有权转移给 Worker。
handoff 失败时 Acceptor 负责关闭 fd 并记录指标。
不得把正式 bufferevent 从 Acceptor event_base 迁移到 Worker event_base。
```

## 4. Worker 所有权

Worker 独占：

```text
event_base
worker thread
session objects
bufferevent pointers
mount subscription map
source session map
client session map
near session map
relay_pull session map
relay_push session map
client status map shard
server/source status map shard
stream counters shard
RTCM decoders shard
Redis async command context
Redis async subscribe context
auth/config local cache shard or replica
```

Runtime 可读聚合：

```text
worker metrics snapshot
worker state
mount owner registry metadata
draining flag
load summary
```

禁止：

```text
Runtime 或其他 Worker 直接遍历并修改某个 Worker 的 session map。
跨 Worker 传递裸 session 指针。
跨 Worker 传递 bufferevent 指针。
多个 Worker 共享同一个 hiredis async context。
在非 owner Worker 直接写 owner Worker 的客户端 socket。
```

跨 Worker 通信：

```text
message queue
event_active
pipe
eventfd
lock-free queue with event notification
```

跨 Worker 消息必须以值对象或明确所有权对象表达，不传递未定义生命周期的引用。

## 5. Redis Context 规则

hiredis async context 绑定 event loop。v2 规则：

```text
每个 Worker 维护自己的 Redis command context。
每个 Worker 维护自己的 Redis subscribe context。
RuntimeManager 如需 Redis，也使用独立 context。
Agent 不复用 Caster Runtime Redis context。
```

第一版 Worker Redis 建议：

```text
worker_redis_cmd_context
worker_redis_sub_context
```

如果鉴权 cache miss 需要 Redis 查询：

```text
auth_projection_context 可合并到 worker_redis_cmd_context，也可独立。
不得访问 PostgreSQL。
```

## 6. Mount Ownership

核心映射：

```text
mount -> worker_id
```

选择输入：

```text
worker draining flag
active_mount_count
connection_count
source_count
client_count
send_bps
recv_bps
loop_delay_ms_p95
```

第一版策略：

```text
过滤 draining Worker。
若 mount 已有 owner，继续使用原 owner。
若 mount 无 owner，按 active_mount_count 和 connection_count 加权选择低负载 Worker。
source 离线且无 client 后释放 owner。
已有连接不跨 Worker 迁移。
```

理由：

```text
同一 mount 的 source 和 clients 尽量在同一 Worker。
本地 fan-out 不经 Redis 回环。
减少跨线程共享状态。
避免在线 TCP/session 迁移复杂度。
```

## 7. Session 类型

第一版 session 类型：

```text
source session
client session
near session
relay_pull session
relay_push session
```

session 所属 Worker 在创建后保持不变，直到断开销毁。

session 通用字段：

```text
connect_key
runtime_id
worker_id
mount
remote_addr
access_account_id
owner_account_id
connected_at
last_data_at
bytes_in
bytes_out
disconnect_reason
```

disconnect_reason 建议：

```text
client_closed
server_closed
auth_failed
account_disabled
access_account_disabled
balance_insufficient
subscription_expired
group_revoked
concurrency_reduced
admin_kick
slow_client
runtime_draining
worker_draining
redis_unavailable
unknown
```

## 8. 数据路径

### 8.1 Source 数据进入

```text
on_source_data(mount, bytes):
  update recv counters
  maybe_decode_rtcm(mount, bytes)
  local_subscribers = mount_subscriptions[mount]
  fan-out to local_subscribers
  publish to Redis stream:mount:<mount>
  periodically refresh runtime/session TTL
```

优化约束：

```text
遍历订阅者时不得整体复制大型 map。
如需防止回调中删除，使用 connection id snapshot 或 delayed delete queue。
非 chunked 写入优先 bufferevent_write。
慢客户端 output buffer 超限后断开。
RTCM decode 采样或最小间隔执行，不对每帧做重逻辑。
统计按周期聚合刷新，不在每帧写 PG。
```

### 8.2 Client 订阅

```text
1. Acceptor 解析 mount。
2. WorkerManager 找到 owner(mount)。
3. handoff 到 owner Worker。
4. Worker 执行 auth 和访问控制。
5. Worker 加入 mount_subscriptions[mount]。
6. Source 数据到达后本地 fan-out。
```

### 8.3 Redis Pub/Sub

本地 Runtime 内：

```text
source -> owner Worker -> local fan-out
source -> owner Worker -> Redis publish
```

跨 Runtime / 跨节点：

```text
Redis Pub/Sub -> subscribed Worker -> local clients
```

最小 Redis bus 契约：

```text
channel: stream:mount:<mount>
payload: NCV2BUS1 envelope + raw bytes
origin_runtime_id: envelope field; subscribed runtime ignores its own messages
```

本 channel 不兼容旧 `MPT:<mount>` channel。Worker 只在本地 client 订阅 mount 时订阅
对应当前 channel；source 数据进入后先本地 fan-out，再发布到 Redis。Redis subscribe 回调
只能在该 Worker 的 event loop 内向本 Worker client fan-out，不得跨 Worker 访问 session map。

第一版保留普通 Pub/Sub。只有当指标显示 Redis 成为共享瓶颈时，再评估 Sharded Pub/Sub。

### 8.4 SourceTable 请求

源列表请求使用同一 NTRIP acceptor 入口，但进入 worker 后作为特殊短连接
SourceTable client 处理：

```text
GET /
  -> Acceptor 解析为 ConnectType::SourceTable
  -> WorkerManager 选择一个非 draining worker
  -> Worker 创建 SourceTable session
  -> session 读取 ClusterSourcetableCache immutable snapshot
  -> 构建 sourcetable 响应并关闭连接
```

SourceTable session 不绑定真实 mount owner，不加入 `_clients` / `mount_subscriptions`，
不订阅 `stream:mount:<mount>`，不计入普通 `client_count`。Runtime metrics 单独暴露
`sourcetable_request_count`、`sourcetable_cache_entry_count` 和 `sourcetable_cache_age_ms`。

每个 Caster 维护本地 `ClusterSourcetableCache`。本 runtime 从 worker 内 source 上线、
下线和位置更新事件维护本地源列表，并通过 `sourcetable:runtime:<runtime_id>` 发布短 TTL
快照，同时发布 `sourcetable:changed`。其他 runtime 的快照通过订阅
`sourcetable:changed` 进入本地 cache。源列表请求路径只能读取当前 cache snapshot，
不得 Redis scan，也不得跨 worker 遍历 live session/map。

## 9. Worker Count 调整

### 9.1 扩容

```text
worker_count: 4 -> 8
```

流程：

```text
1. Agent 或 Runtime local API 收到目标 worker_count。
2. WorkerManager 创建新 Worker 线程和 event_base。
3. 新 Worker 初始化 Redis context、cache、timer。
4. mount owner registry 保持既有 mount 不变。
5. 新 mount 可以分配到新增 Worker。
6. metrics 展示新 Worker 负载。
```

不做：

```text
不迁移已有 TCP 连接。
不强制把已有 mount 重新均衡。
```

### 9.2 缩容

```text
worker_count: 8 -> 4
```

第一版策略：

```text
目标 Worker 设置 draining。
draining Worker 不再接收新 mount。
已有连接继续运行。
连接自然断开后释放 mount owner。
Worker 无活跃 session 后停止 event_base。
```

禁止：

```text
直接杀死有活跃 session 的 Worker。
跨 Worker 搬迁 bufferevent。
把缩容伪装成已完成，实际 Worker 仍有活跃连接却未标注 draining。
```

## 10. Draining

Runtime draining：

```text
Runtime 不再接收新业务连接或只允许健康检查。
已存在连接继续运行，除非管理员选择强制停止。
```

Worker draining：

```text
WorkerManager 不再把新 mount 分配给该 Worker。
已有 mount 和 session 继续运行。
source 离线且无 client 后释放 owner。
Worker active_session_count=0 后关闭。
```

Web 展示：

```text
draining=true
active_sessions
active_mounts
estimated_empty_at 可选
```

## 11. Local Runtime API

本机 API 只供 Agent 调用。

```text
GET  /api/v1/runtime-local/health
GET  /api/v1/runtime-local/metrics
POST /api/v1/runtime-local/reload
POST /api/v1/runtime-local/workers/scale
POST /api/v1/runtime-local/workers/{worker_id}/drain
POST /api/v1/runtime-local/drain
POST /api/v1/runtime-local/shutdown
```

认证：

```text
绑定 127.0.0.1 或本机命名管道 / Unix socket。
使用 Agent 渲染的 local token。
不接受 Web token。
不暴露公网。
```

## 12. Metrics

Runtime metrics：

```text
runtime_id
process_id
uptime_seconds
listen_port
worker_count
connection_count
source_count
client_count
send_bps
recv_bps
redis_connected
```

Worker metrics：

```text
worker_id
state
draining
event_loop_delay_ms_p50
event_loop_delay_ms_p95
event_loop_delay_ms_p99
mount_count
source_count
client_count
fanout_count_per_sec
fanout_cost_us_p95
redis_publish_count
redis_sub_count
slow_client_disconnect_count
output_buffer_limit_count
```

Mount metrics：

```text
mount
owner_worker_id
source_online
subscriber_count
input_bps
output_bps
last_data_at
```

## 13. PG 禁止边界

以下路径不得访问 PostgreSQL：

```text
acceptor header parse
handoff
auth hot path
source data callback
client write callback
Redis sub callback
RTCM decode callback
session timer tick
local fan-out
```

允许进入 PG 的方式：

```text
Runtime 通过 Redis runtime state / event 汇聚，由 AdminService 或后台任务异步写 PG。
计费事实可通过异步队列或 Redis stream/outbox 进入 AdminService，再写 PG。
```

## 14. Review 检查清单

Reviewer 应专项检查：

```text
是否有 Worker 外部直接访问 session map。
是否有跨 event_base 使用 bufferevent。
是否共享 hiredis async context。
handoff 失败是否关闭 fd。
handoff 成功后 Acceptor 是否不再访问 fd。
initial_bytes 是否完整交给 Worker parser。
draining Worker 是否仍接收新 mount。
worker_count 降低是否强杀活跃 session。
Caster 热路径是否引入 PG 或 AdminService 同步调用。
Redis callbacks 是否复制大型订阅 map。
慢客户端 output buffer 是否有上限。
```

## 15. 后续验证要求

后续实现至少覆盖：

```text
mount owner selection unit test。
Acceptor handoff fd ownership test。
initial_bytes parser continuation test。
Worker 独立 Redis context 检查。
Worker draining 不接新 mount。
worker_count 增加后新 mount 可进入新 Worker。
worker_count 降低时活跃连接自然排空。
source -> local clients fan-out。
source -> Redis publish -> other Runtime clients。
慢客户端断开。
热路径无 PG 依赖的静态检查或代码审查。
```
