# NavCaster 数据结构与工作流文档

> 本文档描述 NavCaster 的内部数据结构、Redis 存储模型、数据流路径以及系统线程模型。
>
> 可信度提示：本文部分内容是早期架构说明，其中“单 event_base/单线程事件循环”
> 描述已落后于当前源码。当前线程模型、HTTP/SSE 运行事实优先参考
> `doc/workflow.md`、`doc/project-memory.md` 和源码。Redis/数据流章节仍可作为
> 历史参考，但进入开发任务前必须交叉核对 `doc/redis-schema-v2.md` 和当前代码。

---

## 目录

- [架构概览](#架构概览)
- [线程模型](#线程模型)
- [模块结构](#模块结构)
- [Redis 数据结构](#redis-数据结构)
- [Pub/Sub 频道](#pubsub-频道)
- [核心数据流](#核心数据流)
- [SSE 推送机制](#sse-推送机制)
- [集群协调机制](#集群协调机制)
- [已知架构约束](#已知架构约束)

---

## 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                    CasterService (单进程)                  │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │  NTRIP   │  │   HTTP   │  │   SSE    │  │ Process  │ │
│  │ Listener │  │  Server  │  │ Manager  │  │  Queue   │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘ │
│       │              │              │              │      │
│       └──────────────┴──────────────┴──────────────┘      │
│                          │                                │
│                   event_base (单线程)                      │
│                          │                                │
│  ┌───────────────────────┴───────────────────────┐       │
│  │              CasterCore (caster_internal)       │       │
│  │  ┌─────────┐  ┌──────────┐  ┌──────────────┐  │       │
│  │  │  Redis   │  │  Redis   │  │    状态管理    │  │       │
│  │  │  Async   │  │  Sub     │  │  + 解码       │  │       │
│  │  │ (Pub/Cmd)│  │ (Data)   │  │  + 集群       │  │       │
│  │  └─────────┘  └──────────┘  └──────────────┘  │       │
│  └───────────────────────────────────────────────┘       │
└──────────────────────────┬───────────────────────────────┘
                           │
                      ┌────┴────┐
                      │  Redis  │
                      │ Server  │
                      └─────────┘
```

**技术栈**：C++20 / libevent2 / hiredis / protobuf / nlohmann/json / spdlog / yaml-cpp

---

## 线程模型

### 单线程事件循环

**整个程序运行在一个 `event_base_dispatch()` 主循环上，没有独立线程。**

启动流程：
```
main() → ntrip_config::Init()      // 加载 YAML 配置
       → ntrip_caster::start()
           → component_init()      // 初始化核心组件
           → extra_init()          // 初始化 HTTP/SSE
           → event_base_dispatch() // 进入事件循环（阻塞）
```

### 共享 event_base 的组件

| 组件 | 注册方式 | 职责 |
|------|----------|------|
| **NTRIP Listener** | `evconnlistener_new_bind(_base)` | TCP 监听，协议解析 |
| **HTTP Server** | `evhttp_new(_base)` | REST API 服务 |
| **SSE Manager** | `event_new(_base)` 定时器 | 实时推送（2s 间隔） |
| **Process Queue** | `event_new(_base)` | 请求分发队列 |
| **CasterCore Pub** | `redisLibeventAttach(_base)` | Redis 异步发布/命令 |
| **CasterCore Sub** | `redisLibeventAttach(_base)` | Redis 订阅回调 |
| **Periodic Timer** | `event_new(_base)` | 状态上传/心跳 |

### 消息队列（Process Queue）

NTRIP Listener 解析完请求后不直接处理，而是通过队列传递：

```
Listener → QUEUE::Push(ConnectInfo) → event_active()
                                          ↓
        ntrip_caster::Request_Process_Cb → QUEUE::Pop()
                                          ↓
                        process_request(ConnectInfo)
```

> 无锁设计：`std::queue<ConnectInfo>` 无 mutex，因为只在同一 event_base 线程中操作。

---

## 模块结构

### 源码目录

```
src/
├── service/                 # CasterService 可执行文件
│   ├── main.cpp             # 入口
│   ├── ntrip_caster.h/cpp   # 核心调度器（单例，持有 event_base）
│   ├── ntrip_listener.h/cpp # NTRIP TCP 监听，协议解析
│   ├── ntrip_config.h/cpp   # YAML 配置加载
│   ├── connect_bev.h/cpp    # bufferevent 管理
│   ├── process_queue.h/cpp  # 事件驱动消息队列
│   ├── ntrip_global.h       # Carrier 容器模板
│   ├── logger.h/cpp         # 数据日志记录
│   ├── session/             # 会话类型（协程）
│   │   ├── carrier_base.h   # 会话基类
│   │   ├── server_ntrip.h   # 基站会话
│   │   ├── client_ntrip.h   # 普通用户会话
│   │   ├── client_near.h    # 最近基站用户会话
│   │   ├── source_ntrip.h   # 源列表请求会话
│   │   ├── relay_pull.h     # Pull 转发会话
│   │   └── relay_push.h     # Push 转发会话
│   └── extra/               # 扩展模块
│       ├── license_check/   # 许可证检查
│       ├── heart_beat/      # 心跳上传
│       └── info_upload/     # 信息上传
│
├── http/                    # casterhttp 库
│   ├── http_server.h/cpp    # evhttp 封装（路由匹配、CORS、静态文件）
│   ├── http_handler.h/cpp   # 全部 API 处理逻辑 + sync_redis
│   ├── sse_manager.h/cpp    # SSE 推送管理
│   └── redis_adapter.h/cpp  # 异步 Redis 适配器
│
├── core/                    # castercore 库
│   ├── include/
│   │   ├── Caster_Core.h    # 对外 C 风格 API（CASTER:: 命名空间）
│   │   └── proto_json.h     # Protobuf ↔ JSON 工具
│   ├── src/
│   │   ├── caster_internal.h/cpp  # 核心实现
│   │   └── ...
│   └── context/             # 数据结构定义
│       ├── server_status.h, client_status.h, stream_status.h
│       ├── source_record.h, alias_rule.h
│       ├── access_group.h, access_item.h
│       ├── pull_record.h, pull_status.h
│       ├── push_record.h, push_status.h
│       └── caster_node.h, broadcast_msg.h
│
├── base/                    # casterbase 库
│   ├── decode_rtcm.h/cpp    # RTCM 数据解码
│   ├── decode_nmea.h/cpp    # NMEA GGA 解码
│   ├── nmea0183.h/cpp       # NMEA 协议解析
│   ├── base64.h/cpp         # Base64 编解码
│   └── network.h/cpp        # 网络工具
│
└── auth/                    # authverify 库
    └── ...                  # 账户鉴权
```

### 库依赖关系

```
CasterService
  ├── casterhttp  (HTTP API + SSE)
  ├── castercore  (核心逻辑 + Redis)
  ├── casterbase  (协议解码)
  └── authverify  (鉴权)
```

---

## Redis 数据结构

### 概览

NavCaster 使用两个 Redis 实例：
- **Caster Redis**：核心数据（挂载点、用户、流、节点等）
- **Auth Redis**：账户数据（账户记录、活跃状态）

### 配置与管理类

| Redis Key | 类型 | 数据内容 | 生命周期 |
|-----------|------|----------|----------|
| `CONF:SERVICE` | HASH | 服务配置 JSON | 手动管理 |
| `CONF:CORE` | HASH | Core 配置 JSON | 手动管理 |
| `CONF:AUTH` | HASH | 认证配置（含 admin 凭据） | 手动管理 |
| `MPT:RECORD` | HASH | 手动配置的源表记录 | 手动 CRUD |
| `ALIAS:RULE` | HASH | 别名映射规则 (JSON value) | 手动 CRUD |
| `ACCESS:GROUP` | HASH | 访问控制组定义 | 手动 CRUD |
| `ACCESS:ITEM:<gid>` | HASH | 指定组的访问项列表 | 手动 CRUD |
| `PULL:RECORD` | HASH | Pull 中继任务配置 | 手动 CRUD |
| `PUSH:RECORD` | HASH | Push 中继任务配置 | 手动 CRUD |
| `ACT:RECORD` | HASH | 用户账户记录 (auth Redis) | 手动 CRUD |

**数据格式**：每个 Hash 的 field = 资源 UID，value = JSON 字符串。

```
HSET MPT:RECORD "RTCM3_GPS" '{"mountpoint":"RTCM3_GPS","format":"RTCM 3.3",...}'
HSET ALIAS:RULE "alias_001" '{"alias_name":"VRS01","source_name":"RTCM3_GPS","enable":true,"visible":true}'
```

### 运行时状态类

| Redis Key | 类型 | 数据内容 | 生命周期 |
|-----------|------|----------|----------|
| `CASTER:MASTER` | STRING | 主节点 ID | NX + EX 自动过期 |
| `CASTER:NODE` | HASH | 各节点状态 (field=node_id) | HSETEX 自动过期 |
| `MPT:LIST` | HASH | 在线挂载点 (field=mount) | 连接时写入，断开时删除 |
| `USR:LIST` | HASH | 在线用户 (field=username) | 连接时写入，断开时删除 |
| `MPT:STAT` | HASH | 基站连接状态 (field=uid) | HSETEX 自动过期 |
| `USR:STAT` | HASH | 用户连接状态 (field=uid) | HSETEX 自动过期 |
| `STR:STAT` | HASH | 数据流统计 (field=uid) | HSETEX 自动过期 |
| `STR:ACTIVE` | HASH | 活跃账户 (auth Redis) | HSETEX 自动过期 |
| `PULL:STAT` | HASH | Pull 中继状态 (field=uid) | HSETEX 自动过期 |
| `PUSH:STAT` | HASH | Push 中继状态 (field=uid) | HSETEX 自动过期 |

**HSETEX 机制**：状态数据使用 Hash field 级别的过期时间（`HEXPIRE`），节点定时续期。节点宕机后字段自动过期清除。

### 连接与订阅类

| Redis Key | 类型 | 数据内容 | 生命周期 |
|-----------|------|----------|----------|
| `MPT:REC:<mount>` | HASH | 指定挂载点的连接列表 | 会话存续期间 |
| `USR:REC:<user>` | HASH | 指定用户的连接列表 | 会话存续期间 |
| `MPT:SUB:<mount>` | HASH | 指定挂载点的订阅者列表 | 订阅期间 |
| `USR:SUB:<user>` | HASH | 指定用户的订阅者列表 | 订阅期间 |

### 地理位置类

| Redis Key | 类型 | 数据内容 | 用途 |
|-----------|------|----------|------|
| `MPT:GEO` | GEO (ZSET) | 挂载点经纬度 | 基站位置索引 |
| `USR:GEO` | GEO (ZSET) | 用户经纬度 | 最近基站查询 |

**最近基站查询**：
```
GEORADIUS MPT:GEO <lon> <lat> 100 km COUNT 1 ASC
```

### 解码数据类

| Redis Key | 类型 | 数据内容 | 来源 |
|-----------|------|----------|------|
| `MPT:SOURCE` | HASH | 自动解码的源信息 (field=mount) | RTCM 数据解码自动生成 |

---

## Pub/Sub 频道

### 数据分发频道

| 频道模式 | 数据内容 | 发布者 | 订阅者 |
|----------|----------|--------|--------|
| `MPT:<mount>` | RTCM 二进制数据 | 基站会话 (server_ntrip) | 用户会话 (client_ntrip/near) |
| `USR:<user>` | 用户上传数据 (GGA 等) | 用户会话 | 需要用户数据的会话 |

### 集群协调频道

| 频道 | 数据内容 | 发布者 | 订阅者 |
|------|----------|--------|--------|
| `CASTER:BROADCAST` | 状态变更通知 (Protobuf) | 任意节点 | 所有节点 |
| `NODE:<node_id>` | Relay 任务指令 | 主节点 | 目标节点 |

**广播消息类型**（BroadcastMsg）：
- 挂载点上/下线通知
- 用户连接/断开通知
- Relay 任务启停指令
- 别名规则/访问控制变更通知

---

## 核心数据流

### 1. 基站数据转发（Server → Client）

这是系统的核心数据路径：

```
NTRIP 基站 (TCP)
    │
    ▼
ntrip_listener::Accept_Client_Cb()     ← evconnlistener 回调
    │  协议解析 (HTTP/NTRIP)
    │  GGA 位置解析
    │  生成 ConnectInfo (Protobuf)
    ▼
QUEUE::Push(ConnectInfo)                ← 入队
    │  event_active() 唤醒
    ▼
ntrip_caster::process_request()         ← 出队处理
    │  根据 connect_type 创建会话
    ▼
server_ntrip::run()                     ← 基站协程启动
    │  CASTER::Register_Record()        → 注册到 Redis
    │  CASTER::Pub_Source_Table()        → 上传源表
    │  循环：BevRead() 等待数据
    ▼
CASTER::Pub_Base_Data(mount, data, len) ← 每收到一帧调用
    │
    ▼
caster_internal::pub_base_channel()
    ├── decode_rtcm()                   → 解析坐标/报文类型
    ├── GEOADD MPT:GEO                  → 更新基站位置
    ├── HSETEX MPT:STAT                 → 更新基站状态
    ├── HSETEX STR:STAT                 → 更新流统计
    └── PUBLISH MPT:<mount> data        → Redis 发布数据
                    │
                    ▼
    Redis_SUB_Base_Callback()           ← Redis 订阅回调
        │  在 _base_sub_map[channel] 中查找订阅者
        ▼
    遍历所有订阅者回调
        │  cb(channel, subscriber_key, &Reply)
        ▼
    写入 client 的 bufferevent          ← TCP 发送给用户
```

### 2. 用户连接流程

```
NTRIP 用户 (TCP)
    │
    ▼
ntrip_listener → 解析请求
    │  判断连接类型：CLIENT / NEAREST
    │  解析 GGA 位置（从 Ntrip-GGA 头或 body）
    ▼
QUEUE → process_request()
    │  创建 client_ntrip 或 client_near
    ▼
client_ntrip::run() / client_near::run()
    ├── CASTER::Register_Record()        → 注册
    ├── CASTER::Sub_Raw_Data(mount)      → 订阅数据
    │       └── sub_base_channel()
    │           ├── 检查是否为别名 → sub_alias_channel()
    │           └── SUBSCRIBE MPT:<mount>
    │               └── 回调加入 _base_sub_map
    └── 循环：BevRead() 读取用户上行数据
            └── CASTER::Pub_Raw_Data()   → 转发给基站（反向通道）
```

### 3. 别名订阅流程

```
用户请求挂载点 "VRS01"（别名）
    │
    ▼
CASTER::Sub_Raw_Data("VRS01")
    │
    ▼
sub_base_channel("VRS01")
    ├── 查找 _alias_rule_map["VRS01"]
    │   found → source = "RTCM3_GPS"
    ▼
sub_alias_channel("VRS01", "RTCM3_GPS")
    └── 实际订阅 SUBSCRIBE MPT:RTCM3_GPS
        └── 回调 key 仍为用户的 subscriber key
```

### 4. 最近基站查询流程

```
用户请求 NEAREST 模式，携带 GGA 位置
    │
    ▼
client_near::run()
    ├── GEORADIUS MPT:GEO lon lat 100 km COUNT 1 ASC
    │   → 返回最近的挂载点名称
    ├── SUBSCRIBE MPT:<nearest_mount>
    └── 定期更新位置 → 可能切换基站
```

### 5. Relay 数据流（Pull / Push）

**Pull 中继**（从远程拉取到本地）：
```
远程 NTRIP Caster
    │
    ▼ (TCP 连接 + NTRIP 协议)
relay_pull 会话
    │  接收 RTCM 数据
    ▼
CASTER::Pub_Base_Data()                → 当作本地基站发布
    └── PUBLISH MPT:<local_mount>
```

**Push 中继**（从本地推送到远程）：
```
本地挂载点数据 (SUBSCRIBE MPT:<mount>)
    │
    ▼
relay_push 会话
    │  接收本地数据
    ▼ (TCP 连接 + NTRIP 协议)
远程 NTRIP Caster                      → 作为基站推送
```

---

## SSE 推送机制

### 工作原理

```
Web 浏览器
    │ GET /api/events/stream?token=xxx
    ▼
sse_manager::add_client()
    ├── evhttp_send_reply_start(200, chunked)
    ├── 发送初始快照（所有频道当前数据）
    └── 加入客户端列表
    
每 2 秒定时器触发:
    ▼
poll_and_broadcast()
    ├── 无客户端 → 跳过
    ├── 遍历 16 个频道:
    │   ├── sync_redis::hgetall(KEY)     ← 阻塞读取
    │   ├── 对比 cached_data
    │   └── 有变化 → broadcast()
    └── send_sse_event → evhttp_send_reply_chunk
```

### SSE 频道与 Redis Key 映射

| SSE 频道 | Redis Key | 更新频率 |
|----------|-----------|----------|
| `servers` | `MPT:STAT` | 基站上/下线或状态变化 |
| `clients` | `USR:STAT` | 用户连接/断开 |
| `streams` | `STR:STAT` | 数据流统计变化 |
| `nodes` | `CASTER:NODE` | 节点心跳/上下线 |
| `accounts` | `ACT:RECORD` | 账户增删改 |
| `sources` | `MPT:RECORD` | 源表记录变化 |
| `aliases` | `ALIAS:RULE` | 别名规则变化 |
| `access_groups` | `ACCESS:GROUP` | 权限组变化 |
| `pull_records` | `PULL:RECORD` | Pull 配置变化 |
| `pull_states` | `PULL:STAT` | Pull 运行状态变化 |
| `push_records` | `PUSH:RECORD` | Push 配置变化 |
| `push_states` | `PUSH:STAT` | Push 运行状态变化 |
| `account_actives` | `STR:ACTIVE` | 账户活跃状态变化 |

---

## 集群协调机制

### 主节点选举

```
启动时:
    SET CASTER:MASTER <node_id> NX EX 30
        │
        ├── 成功 → 当前节点为 Master
        │   └── 定时续期 (SET ... XX EX 30)
        │
        └── 失败 → 当前节点为 Slave
            └── 定时检测 Master 是否过期
```

### 节点状态上报

每个节点定时执行：
```
HSETEX CASTER:NODE <node_id> <ttl> <status_json>
```

### 广播通信

```
节点 A (挂载点上线)
    │ PUBLISH CASTER:BROADCAST <BroadcastMsg>
    ▼
所有节点 SUBSCRIBE CASTER:BROADCAST
    │ 回调处理广播消息
    ▼
更新本地缓存（_mpt_online_map 等）
```

### Relay 任务调度

Master 节点检测到 Pull/Push Record 变化时：
```
Master:
    1. 读取 PULL:RECORD / PUSH:RECORD
    2. 对比 PULL:STAT / PUSH:STAT
    3. 找到 enabled=true 但未运行的任务
    4. PUBLISH NODE:<target_node_id> <relay_task>

Target Node:
    SUBSCRIBE NODE:<self_node_id>
    → 收到任务 → 创建 relay_pull/relay_push 会话
```

---

## 已知架构约束

### 1. 单线程阻塞风险

**所有组件共享同一个 event_base**，任何阻塞操作都会暂停整个系统：

| 阻塞源 | 阻塞时间 | 影响范围 | 当前状态 |
|--------|----------|----------|----------|
| `sync_redis` 调用 | < 1ms（本地 Redis） | HTTP API 响应期间 | 可接受 |
| `handle_fetch_sourcetable` | 最多 5+ 秒 | 远程 TCP 连接 | ⚠️ 高风险 |
| SSE `poll_and_broadcast` | ~13 次 HGETALL | 每 2 秒一次 | 可监控 |
| `getaddrinfo()` | 不确定 | DNS 解析 | ⚠️ 无法控制 |

**缓解措施**：
- `handle_local_sourcetable` 直接调用 Core 接口，避免网络阻塞
- `sync_redis` 连接本地 Redis，延迟极低
- 考虑未来将 HTTP 服务移到独立线程

### 2. sync_redis 设计权衡

HTTP handler 使用**同步阻塞** Redis 客户端而非异步：
- **原因**：HTTP API 仅用于管理界面，请求频率低；所有操作为 Hash 操作，本地 Redis 延迟 < 1ms
- **风险**：Redis 不可用时阻塞整个事件循环
- **改进方向**：引入异步 Redis pipeline 或将 HTTP 服务独立线程化

### 3. SSE 轮询开销

每 2 秒对 16 个频道执行 `HGETALL`，数据量大时可能影响性能。改进方向：
- Redis Keyspace Notification 替代轮询
- 仅查询有 SSE 客户端订阅的频道
- 增大轮询间隔或引入自适应间隔

### 4. 无持久化的会话状态

当前连接上/下线记录仅存在于 Redis（带 TTL 自动过期），程序崩溃后：
- 状态字段自动过期（通过 HEXPIRE），不会残留脏数据
- 但没有历史记录可追溯

### 5. 无锁设计的限制

Process Queue、会话容器等均无锁，依赖单线程保证安全。若未来引入多线程，需要全面审查并发安全。
