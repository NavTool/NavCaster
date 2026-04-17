# NavCaster 工程架构分析与优化计划

> 文档版本：v1.0  
> 创建日期：2026-04-16  
> 最后更新：2026-04-16

---

## 目录

1. [工程概述](#1-工程概述)
2. [系统架构](#2-系统架构)
3. [模块详解](#3-模块详解)
4. [Proto 协议定义](#4-proto-协议定义)
5. [Redis 数据模型](#5-redis-数据模型)
6. [已知问题清单](#6-已知问题清单)
7. [优化改进方案](#7-优化改进方案)
8. [执行计划与里程碑](#8-执行计划与里程碑)

---

## 1. 工程概述

NavCaster 是一套 NTRIP Caster 系统，支持 NTRIP 1.0/2.0 协议，用于 GNSS 差分数据的分发与管理。系统由以下主要部分组成：

| 组件 | 技术栈 | 说明 |
|------|--------|------|
| **CasterService** | C++20 / libevent / hiredis / protobuf | NTRIP 核心服务，处理基站/移动站连接、数据转发，内置 HTTP API 服务器 |
| **CasterWeb** | React 18 / TypeScript / Ant Design / Vite | Web 管理前端，通过 HTTP API + SSE 监控和管理配置 |
| **caster_core** (lib) | C++20 / libevent / hiredis | 核心库，挂载点管理、Redis Pub/Sub 数据分发 |
| **auth_verify** (lib) | C++20 / libevent / hiredis | 认证库，用户验证与登录管理 |
| **caster_base** (lib) | C++20 | 基础工具库，Base64/NMEA/RTCM 解码、网络工具 |
| **caster_proto** (lib) | Protobuf | 协议定义，跨组件消息序列化 |
| **Dev Tools** | C++17 | ntrip_client_sim / ntrip_server_sim / strsvr_mult |

---

## 2. 系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                    CasterWeb (React/TypeScript 管理端)            │
│                                                                 │
│  Pages ← Hooks (useSSE/useMultiSSE) ← HTTP API + SSE            │
└────────────────────────────┬────────────────────────────────────┘
                             │ HTTP API / SSE
┌────────────────────────────▼────────────────────────────────────┐
│                     Redis (中间件/数据总线)                      │
│                                                                 │
│  HASH: MPT:* / USR:* / ACT:* / STR:* / CASTER:*               │
│  PUB/SUB: MPT:<NODE> / USR:<NODE> / CASTER:BROADCAST           │
│  GEO: MPT:GEO / USR:GEO                                        │
└────────────────────────────┬────────────────────────────────────┘
                             │ Redis (hiredis async)
┌────────────────────────────▼────────────────────────────────────┐
│                 CasterService (C++20 NTRIP 服务)                │
│                                                                 │
│  ntrip_listener (TCP) → ProcessQueue → ntrip_caster (路由)     │
│                                              ↓                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐          │
│  │ server_  │ │ client_  │ │ source_  │ │ client_  │          │
│  │ ntrip    │ │ ntrip    │ │ ntrip    │ │ near     │          │
│  │ (基站)   │ │ (移动站) │ │ (源列表) │ │ (最近站) │          │
│  └────┬─────┘ └────┬─────┘ └──────────┘ └──────────┘          │
│  ┌────┴─────┐ ┌────┴─────┐                                     │
│  │ relay_   │ │ relay_   │      所有 Carrier 使用               │
│  │ pull     │ │ push     │      C++20 协程 + EventChannel      │
│  │ (拉取)   │ │ (推送)   │                                     │
│  └────┬─────┘ └────┬─────┘                                     │
│       └──────┬─────┘                                            │
│  ┌───────────▼──────────────────────────────────────────────┐  │
│  │ lib/caster_core  (CASTER:: 命名空间)                      │  │
│  │   Redis Pub/Sub 数据分发 | 挂载点注册/撤销 | 状态上报     │  │
│  ├──────────────────────────────────────────────────────────┤  │
│  │ lib/auth_verify  (AUTH:: 命名空间)                        │  │
│  │   用户密码验证 | 登录记录 | 连接数限制                    │  │
│  ├──────────────────────────────────────────────────────────┤  │
│  │ lib/caster_base                                           │  │
│  │   Base64 | NMEA/RTCM解码 | 网络工具 | 坐标转换           │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 核心数据流

| 数据路径 | 流向 |
|---------|------|
| **基站上行** | 基站 → TCP → `ntrip_listener` → AUTH 验证 → `server_ntrip` 协程 → RTCM 解码 → `CASTER::Pub_Base_Raw_Data` → Redis PUB |
| **移动站下行** | Redis SUB → `client_ntrip` 协程 → TCP → 移动站客户端 |
| **Relay Pull** | 远端 Caster → TCP → `relay_pull` 协程 → `CASTER::Pub_Base_Raw_Data` → 本地 Redis |
| **Relay Push** | 本地 Redis SUB → `relay_push` 协程 → TCP → 远端 Caster |
| **监控管理** | `CasterWeb` → HTTP API → `CasterService` → Redis HASH CRUD → `caster_core` / `auth_verify` 响应配置变更 |

### 2.3 关键设计模式

| 模式 | 应用位置 | 说明 |
|------|---------|------|
| C++20 协程 + libevent | 所有 Carrier | `co_await` 驱动异步 TCP I/O 和 Redis 订阅 |
| EventChannel | carrier_base | 统一事件队列，running 阶段协程消费事件 |
| Singleton | ntrip_caster / ntrip_config / ntrip_listener / connect_bev | 全局单例管理器 |
| Template Container | `Carrier<T>` / `HashConetxt<T>` | 泛型连接管理 / Redis HASH 操作封装 |
| Producer-Consumer | process_queue | listener 线程 → caster 事件循环的跨线程传递 |
| Redis Pub/Sub | caster_core | RTCM/NMEA 数据实时分发 + 集群节点广播 |

### 2.4 线程模型

**CasterService**:
- **主线程**: 配置加载、服务组件初始化
- **event_base 线程**: `event_base_dispatch()` 驱动所有 Carrier I/O（单线程事件循环）
- **跨线程通信**: `process_queue` 使用 `event_active()` 唤醒事件循环

**CasterWeb**:
- 纯前端应用，通过 HTTP API 和 SSE 与 CasterService 通信，无独立后端线程

---

## 3. 模块详解

### 3.1 CasterService

#### 3.1.1 ntrip_listener (连接接入层)

- 监听 TCP 端口，接受 NTRIP 客户端连接
- 解析 HTTP/NTRIP 请求头（支持 GET/POST/SOURCE 方法）
- 支持 NTRIP 1.0 (ICY) 和 NTRIP 2.0 (HTTP chunked)
- 提取 Basic 认证信息，调用 AUTH 服务验证
- 验证通过后将 `ConnectInfo` 推入 `process_queue`

#### 3.1.2 ntrip_caster (路由调度层)

- 从 `process_queue` 取出请求，根据 `ConnectType` 路由到对应 `Carrier<T>`
- 管理 6 类 Carrier 容器：`Servers / Clients / Sources / Nears / Pulls / Pushs`
- 处理周期性任务（状态统计、内存回收）
- 监听 `CASTER:BROADCAST` 处理 Relay 广播请求

#### 3.1.3 Carrier 实现（连接处理层）

| 类名 | 继承 | 职责 | 协程生命周期 |
|------|------|------|-------------|
| `carrier_base` | - | 基类：TCP I/O、认证、注册、订阅 | co_auth → co_register → co_subscribe → event loop |
| `server_ntrip` | carrier_base | 基站数据上行 | AUTH → 注册 → 循环读 RTCM → 发布到 Redis |
| `client_ntrip` | carrier_base | 移动站数据下行 | AUTH → 注册 → 订阅 → 循环转发到 TCP |
| `source_ntrip` | carrier_base | 源列表查询 | 构建源表 → 发送 → 关闭（一次性） |
| `client_near` | carrier_base | 最近基站接入 | AUTH → 注册 → 读 NMEA → 自动切换基站 |
| `relay_pull` | carrier_base | 从远端拉取数据 | 连接远端 → NTRIP GET → 读数据 → 本地发布（带重连） |
| `relay_push` | carrier_base | 向远端推送数据 | 连接远端 → NTRIP POST → 订阅本地 → 转发（带重连） |

#### 3.1.4 Component 层

| 组件 | 说明 |
|------|------|
| `process_queue` | 线程安全队列，`event_active()` 跨线程唤醒 |
| `connect_bev` | bufferevent 注册表，集中管理 TCP 连接、超时定时器 |
| `ntrip_config` | YAML 配置加载（Service_Setting / Caster_Core / Auth_Verify） |

### 3.2 CasterWeb

#### 3.2.1 核心架构

- **技术栈**: React 18 + TypeScript + Ant Design + Vite
- **数据获取**: HTTP REST API + Server-Sent Events (SSE) 实时推送
- **状态管理**: 自定义 `useSSE` / `useMultiSSE` Hooks 管理实时数据流
- **认证**: JWT Token 登录，axios 拦截器自动附带

#### 3.2.2 页面结构

| 页面 | 路由 | 功能 |
|------|--------|------|
| Dashboard | /dashboard | 集群资源概览（CPU/内存/流量）+ 节点状态卡片 |
| Servers | /servers | 基站连接列表 |
| Clients | /clients | 移动站连接列表 |
| Accounts | /accounts | 用户账户 CRUD |
| Sources | /sources | 挂载点管理 |
| Aliases | /aliases | 别名规则 |
| AccessGroups | /access | 访问控制分组 |
| PullRelay | /relay/pull | Pull 中继管理 |
| PushRelay | /relay/push | Push 中继管理 |

### 3.3 共享库

#### lib/caster_core (CASTER:: 命名空间)

核心 API 分为四大类：
- **BASE（基站）**: `Register/Withdraw_Base_Record`, `Pub/Sub_Base_Raw_Data`, `Set_Base_Coord_Info`
- **ROVER（移动站）**: `Register/Withdraw_Rover_Record`, `Pub/Sub_Rover_Raw_Data`, `Set_Rover_Coord_Info`
- **GRID（格网）**: `Register/Withdraw_Grid_Record`, `Pub/Sub_Grid_Raw_Data`
- **管理**: `Get_Source_Table_Text`, `Check_Nearest_Mpt`, `Check_Alias_Mpt`, `Relay_Register_Callback`

#### lib/auth_verify (AUTH:: 命名空间)

- `Verify()`: 异步验证用户名密码
- `Add_Login_Record()`: 记录登录（含连接数限制检查）
- `Add_Logout_Record()`: 注销记录
- 支持 `AuthType::SERVER / CLIENT / SOURCE` 三种认证类型

#### lib/caster_base

纯工具函数库：Base64 编解码、NMEA/RTCM 解码、TCP 工具、坐标转换（WGS84↔ECEF）、系统资源监控。

---

## 4. Proto 协议定义

### 4.1 服务配置 (caster/service/)

| Proto | 消息 | 用途 |
|-------|------|------|
| ConnectInfo.proto | `ConnectInfo` | 连接请求信息（类型/操作/挂载点/认证/地址） |
| CasterCoreOpt.proto | `CasterCoreOpt` | caster_core 配置（Redis/更新间隔/策略） |
| AuthVerifyOpt.proto | `AuthVerifyOpt` | auth_verify 配置（Redis/匿名登录/在线保护） |
| ListenerOpt.proto | `ListenerOpt` | 监听器配置（端口/超时/登录权限） |
| CarrierOpt.proto | `NtripServerOpt / NtripClientOpt` | Carrier 级别配置 |

### 4.2 核心状态 (caster/core/)

| Proto | 消息 | 用途 |
|-------|------|------|
| ServerState.proto | `ServerState` | 基站连接状态 |
| ClientState.proto | `ClientState` | 移动站连接状态 |
| StreamState.proto | `StreamState` | 数据流统计（收发速率） |
| SourceRecord.proto | `SourceRecord` | 挂载点源表记录 |
| AccessGroup.proto | `AccessGroup` | 分组权限配置 |
| AliasRule.proto | `AliasRule` | 别名规则 |
| PullRecord/State.proto | `PullRecord / PullState` | Pull 中继配置与状态 |
| PushRecord/State.proto | `PushRecord / PushState` | Push 中继配置与状态 |
| CasterNode.proto | `CasterNode` | 集群节点信息 |
| BroadcastMsg.proto | `BroadcastMsg` | 节点间广播消息 |

### 4.3 认证 (caster/auth/)

| Proto | 消息 | 用途 |
|-------|------|------|
| AccountRecord.proto | `AccountRecord` | 用户账户信息（类型/状态/过期/连接限制） |
| AccountActive.proto | `AccountActive` | 已激活账户在线记录 |

### 4.4 监控 (caster/monitor/)

| Proto | 消息 | 备注 |
|-------|------|------|
| ServerInfo / ClientInfo / PullInfo / PushInfo 等 | 监控展示用消息 | **与 core/ 下的消息存在大量字段重复，需要整合** |

---

## 5. Redis 数据模型

### 5.1 caster_core 使用的 Redis 键

| Redis 键 | 类型 | 说明 |
|---------|------|------|
| `MPT:REC:<NODE>` | HASH | 基站发布者记录（挂载点→ConnectKey:时间） |
| `MPT:SUB:<NODE>` | HASH | 基站订阅者列表 |
| `MPT:LIST:COMMON` | HASH | 在线普通挂载点 |
| `MPT:LIST:ALIAS` | HASH | 别名挂载点 |
| `MPT:LIST:NEAREST` | HASH | 最近基站挂载点 |
| `MPT:LIST:PROXY` | HASH | 代理挂载点 |
| `MPT:GEO` | GEO | 基站地理索引 |
| `MPT:<NODE>` | PUB/SUB | 基站数据频道 |
| `USR:REC:<NODE>` | HASH | 移动站发布者记录 |
| `USR:SUB:<NODE>` | HASH | 移动站订阅者记录 |
| `USR:LIST:COMMON` | HASH | 移动站用户列表 |
| `USR:GEO` | GEO | 用户位置索引 |
| `USR:<NODE>` | PUB/SUB | 用户数据频道 |
| `GRID:*` | HASH/PUB/SUB | 格网相关键 |
| `CASTER:BROADCAST` | PUB/SUB | 集群广播频道 |
| `CASTER:NODE` | HASH | 集群节点信息 |

### 5.2 auth_verify 使用的 Redis 键

| Redis 键 | 类型 | 说明 |
|---------|------|------|
| `ACT:ACTIVE` | HASH | 已激活账户（带过期） |
| `ACT:ACCOUNT` | HASH | 所有注册账户 |
| `ACT:UNNAMED` | - | 匿名账户 |
| `ACT:REC:<NODE>` | HASH | 登录记录 |

### 5.3 CasterWeb (通过 CasterService HTTP API) 使用的 Redis 键

| Redis 键 | 类型 | 说明 |
|---------|------|------|
| `MPT:RECORD` | HASH | 源站记录 |
| `MPT:STAT` | HASH | 基站状态 |
| `USR:STAT` | HASH | 移动站状态 |
| `STR:STAT` | HASH | 数据流统计 |
| `STR:ALIAS:LIST` | HASH | 别名列表 |
| `STR:PULL:LIST / STAT` | HASH | Pull 中继配置/状态 |
| `STR:PUSH:LIST / STAT` | HASH | Push 中继配置/状态 |
| `ACCESS:GROUP` | HASH | 分组配置 |
| `ACT:RECORD` | HASH | 账户记录 |
| `STR:ACTIVE` | HASH | 活跃账户 |

---

## 6. 已知问题清单

### 6.1 P0 - 严重（内存安全）

| ID | 问题 | 位置 | 说明 | 状态 |
|----|------|------|------|------|
| BUG-001 | chunk_head_data 内存泄漏 | carrier_base.cpp `read_data_from_chunk()` | `evbuffer_readln` 返回的 malloc 内存未 free，每次 chunk 读取都泄漏 | **已修复** |
| BUG-002 | chunk 尾部 CRLF 未消费 | carrier_base.cpp `read_data_from_chunk()` | chunk 数据后的 `\r\n` 未从 evbuffer 移除，导致额外无效递归+二次泄漏 | **已修复** |
| BUG-003 | header new/free 不匹配 | ntrip_listener.cpp `process_bev_request()` | no-CRLF 分支用 `new char[]` 分配但用 `free()` 释放，属未定义行为 | **已修复** |
| BUG-004 | timeval 裸指针管理 | connect_bev.h/cpp | `new timeval` 存入 map，析构函数为空不清理；依赖外部调用 `del_timer` | **已修复** |
| BUG-005 | Singleton 使用 raw new | ntrip_caster/config/listener/connect_bev | `static T *instance = new T()` 永不释放 | **已修复** |

### 6.2 P1 - 高优先级（功能/可维护性）

| ID | 问题 | 位置 | 说明 | 状态 |
|----|------|------|------|------|
| ISSUE-001 | 单元测试覆盖为零 | test/ | 仅 2 个测试文件，核心逻辑无任何测试 | 待处理 |
| ISSUE-002 | client_near 功能未完成 | client_near.h L79 | NMEA 坐标解析 TODO，最近基站自动切换逻辑未实现 | 待处理 |
| ISSUE-003 | 目录名拼写错误 | `Compontent/` → `Component` | 8+ 处引用需修改（目录名+代码引用） | 待处理 |
| ISSUE-004 | monitor Proto 冗余 | proto/caster/monitor/ | `ServerInfo` 等与 core/ 下的 `ServerState` 字段大量重复 | 待处理 |
| ISSUE-005 | 事件日志为 Mock | EventDataController | 占位数据，无真实事件上报 | 待处理 |

### 6.3 P2 - 中优先级（架构/性能）

| ID | 问题 | 说明 | 状态 |
|----|------|------|------|
| ARCH-001 | Monitor 直连 Redis 写入 | 无 API 层隔离，Monitor 和 Service 共享 Redis，存在写冲突风险 | 待处理 |
| ARCH-002 | 单 event_base 瓶颈 | 所有 Carrier I/O 在同一事件循环，大规模连接下成为性能瓶颈 | 待处理 |
| ARCH-003 | Relay 重连策略原始 | 固定 5 秒重试，无指数退避 | 待处理 |
| ARCH-004 | Redis 断线无优雅降级 | Redis 断开后 Carrier 协程行为未定义 | 待处理 |
| ARCH-005 | 死代码/注释代码 | ntrip_caster.cpp 大段注释的 license check 代码 | 待处理 |

### 6.4 P3 - 低优先级（代码规范）

| ID | 问题 | 说明 | 状态 |
|----|------|------|------|
| STYLE-001 | HashConetxt 拼写错误 | 应为 `HashContext` | 待处理 |
| STYLE-002 | 无统一错误码体系 | 函数返回 0/-1，无结构化错误 | 待处理 |
| STYLE-003 | Dev 子项目 C++ 标准不一致 | dev/ 用 C++17，主项目用 C++20 | 待处理 |

---

## 7. 优化改进方案

### 阶段 1：安全加固与基础修复（1-2 周）

| 任务 | 优先级 | 说明 | 依赖 |
|------|--------|------|------|
| 1.1 修复所有内存泄漏 | P0 | BUG-001~004 | 无 |
| 1.2 Singleton 改为 Meyer's 模式 | P0 | BUG-005，去掉 raw new | 无 |
| 1.3 修复 `Compontent` → `Component` | P1 | 全局重命名目录和引用 | 无 |
| 1.4 修复 `HashConetxt` → `HashContext` | P3 | 模板类重命名 | 无 |
| 1.5 清理死代码 | P2 | 移除注释的 license 代码、空 logger 桩 | 无 |

### 阶段 2：核心功能完善（2-3 周）

| 任务 | 优先级 | 说明 | 依赖 |
|------|--------|------|------|
| 2.1 实现 client_near NMEA 解析 | P1 | 利用 decode_nmea 完成坐标提取和基站切换 | 阶段 1 |
| 2.2 实现事件日志系统 | P2 | 替换 Mock EventDataController，接入 Redis Stream | 阶段 1 |
| 2.3 Relay 重连指数退避 | P2 | co_sleep 改为指数退避（5s→10s→20s→60s max） | 阶段 1 |
| 2.4 Redis 断线重连策略 | P2 | caster_core/auth_verify 添加健康检查和自动重连 | 阶段 1 |

### 阶段 3：架构优化（3-4 周）

| 任务 | 优先级 | 说明 | 依赖 |
|------|--------|------|------|
| 3.1 引入 API 层隔离 Monitor | P2 | Monitor 通过 gRPC/HTTP 与 Service 通信 | 阶段 2 |
| 3.2 合并 monitor/core Proto 冗余 | P1 | ServerInfo 复用 ServerState + 扩展字段 | 阶段 2 |
| 3.3 多 event_base 分离 | P2 | Listener 与 Carrier I/O 分到不同线程 | 阶段 2 |
| 3.4 统一错误码体系 | P3 | 定义 ErrorCode 枚举替代 return 0/-1 | 阶段 2 |

### 阶段 4：质量保障（持续）

| 任务 | 优先级 | 说明 | 依赖 |
|------|--------|------|------|
| 4.1 核心模块单元测试 | P1 | caster_core / auth_verify / carrier_base | 阶段 1 |
| 4.2 Carrier 协程集成测试 | P1 | 使用 sim 工具自动化测试 | 阶段 2 |
| 4.3 CI/CD 增強 | P2 | 加入测试、ASan/Valgrind、代码格式检查 | 阶段 4.1 |
| 4.4 性能压测基准 | P3 | 建立连接数/吞吐量基准 | 阶段 3 |

---

## 8. 执行计划与里程碑

```
Week 1-2:  ████ 阶段 1 - 安全修复 + 命名清理
                ✓ BUG-001~005 修复
                ✓ ISSUE-003 目录重命名
                ✓ 死代码清理
                ▸ 里程碑: 零内存泄漏，通过 ASan 检查

Week 3-5:  ████ 阶段 2 - 核心功能完善
                ○ client_near 完整实现
                ○ 事件日志系统落地
                ○ Relay 指数退避重连
                ○ Redis 断线重连
                ▸ 里程碑: 所有 Carrier 功能完整可用

Week 5-8:  ████ 阶段 3 - 架构优化
                ○ Monitor-Service API 层引入
                ○ Proto 整合
                ○ 多线程事件循环
                ▸ 里程碑: 支持 500+ 并发连接无性能退化

Week 8+:   ████ 阶段 4 - 持续质量保障
                ○ 单元测试覆盖 ≥60%
                ○ CI 自动化
                ○ 性能基线建立
                ▸ 里程碑: 完整 CI/CD 流水线
```

---

> **备注**: 本文档将随项目推进持续更新。各阶段完成后，请在对应的 `状态` 列标记完成日期。
