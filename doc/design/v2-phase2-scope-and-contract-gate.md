# NavCaster v2 Phase 2 Scope and Contract Gate

更新时间：2026-06-27
任务：NC-090 v2 Phase 2 Scope And Contract Gate
来源基线：`feature/NC-089-v2-foundation-closed-loop-integration @ 9497a97`
状态：Phase 2 冻结契约，作为 NC-091 到 NC-098 的输入。
适用范围：NavCaster v2 Phase 2 真实控制面闭环、PG/Redis 边界、运行态事件/指标、集成顺序和 QA gate。
可信度：架构 / 文档准入契约；不表示产品代码已全部实现。

相关岗位：

```text
architect
docs-planner
backend-http
backend-core
frontend
qa
reviewer
```

## 1. 冻结结论

Phase 2 从 NC-089 的工程骨架基线继续推进，目标是形成一条真实、可复现、可审查的闭环：

```text
Web intent
  -> AdminService PostgreSQL desired state
  -> AdminService Redis projection
  -> Agent desired polling / reconcile
  -> Agent starts real navcaster-caster
  -> Caster health / metrics / NTRIP data path
  -> Agent runtime-events / runtime-metrics
  -> AdminService actual aggregation
  -> Web desired / actual convergence display
```

Phase 2 只按 v2 契约验收：

```text
不兼容旧架构。
不兼容旧 HTTP API。
不兼容旧 Redis key。
不兼容旧 protobuf。
不兼容旧命名规则。
不兼容旧 NavCaster Web 风格。
不以旧 CasterService 可用性作为通过证据。
```

Phase 2 不等于生产发布：

```text
不承诺 16k 正式容量压测。
不扩展到账务、订阅、支付、供应商结算或生产发布。
不要求 AdminService 高可用。
不引入 gRPC 或旧 protobuf 兼容层。
不把 Redis 升级为长期权威存储。
```

## 2. NC-089 来源基线

本任务冻结的代码来源为：

```text
分支：feature/NC-089-v2-foundation-closed-loop-integration
提交：9497a97 fix NC-089 live control contract gaps
QA：QA_PASSED_WITH_NOTED_GAPS
```

NC-089 已验证：

```text
Admin Go test/build：PASS。
Agent Go test/build：PASS。
navcaster-caster build/self-test：PASS。
v2 minimal NTRIP smoke：PASS。
Web build：PASS。
AdminService health：PASS，postgres/redis=not_configured。
AdminService + Agent once register/desired-state 联调：PASS。
runtime-metrics / runtime-events live contract smoke：PASS。
```

NC-089 未覆盖但 Phase 2 必须关闭或明确记录的缺口：

```text
真实 PostgreSQL + Redis 集成未运行。
Agent 启动真实 navcaster-caster 未运行。
Web 浏览器 live smoke 未在集成 worktree 重跑。
真实 Redis Pub/Sub 跨 Runtime 未运行。
容量压测未运行。
```

## 3. Phase 2 范围

### 3.1 In Scope

Phase 2 必须完成：

```text
AdminService 使用 PostgreSQL 保存 Host、Agent、Runtime desired state、control intent、audit 和 actual/event 历史。
AdminService 生成 Redis projection / runtime TTL / bus，Redis 只作为 projection/cache/bus。
Agent 从 AdminService 拉取 desired state，按版本幂等 reconcile。
Agent 启动、停止、守护本机真实 navcaster-caster 进程。
Agent 采集 Caster health/metrics，并通过 runtime-metrics 上报 actual snapshot。
Agent 通过 runtime-events 上报关键进程和健康事件。
Caster 暴露本机 health/metrics，保留热路径不访问 PostgreSQL。
Caster 使用当前 Redis Pub/Sub 完成跨 Runtime 最小 source/client 链路。
Web 通过真实 AdminService v2 API 展示 Host/Runtime desired vs actual 和 intent 状态。
QA 给出真实 PG/Redis、Admin-Agent-Caster-Web smoke 和未运行项记录规则。
```

### 3.2 Out of Scope

Phase 2 明确不做：

```text
旧 API/key/protobuf 兼容。
旧 Web 页面风格或旧 route 兼容。
账务、订阅、支付、供应商结算、生产发布。
16k 正式容量压测或长时间 soak。
跨物理主机生产网络验证。
AdminService 多副本 HA、leader election 或跨区域部署。
Redis Cluster / Sharded Pub/Sub 产品化。
完整 Auth、计费和供应商业务热路径重构。
```

## 4. Phase 2 集成顺序

第一波任务可以并行开发，但 NC-097 集成必须按契约顺序收口。

| 顺序 | 任务 | 角色 | 必须产出 |
| --- | --- | --- | --- |
| 1 | NC-090 | architect / docs-planner | 本文和 QA gate，冻结 Phase 2 范围和边界。 |
| 2 | NC-091 | backend-http | AdminService PG source-of-truth、Redis projection、actual/event ingest。 |
| 3 | NC-092 | backend-core | Agent 真实 navcaster-caster supervisor、health/metrics 采集和上报。 |
| 4 | NC-093 | backend-core | Caster 跨 Runtime Redis Pub/Sub 最小链路和指标。 |
| 5 | NC-094 | frontend | Web live API 接入，desired/actual 收敛展示和 intent 操作。 |
| 6 | NC-095 | qa | Phase 2 QA matrix、环境准备、smoke 顺序和未运行记录模板。 |
| 7 | NC-097 | integration / qa | 合入 NC-090 到 NC-095，执行闭环 smoke。 |
| 8 | NC-096 | qa / backend-core | 闭环后轻量容量基线，不承诺 16k。 |
| 9 | NC-098 | reviewer | Phase 2 Review，给出 APPROVED / CHANGES_REQUESTED / BLOCKED。 |

并行规则：

```text
NC-092 可以用 stub AdminService 开发，但 DEV_DONE 前必须对齐 NC-091 的 desired/actual/event shape。
NC-093 可以独立验证 Caster，但 NC-097 前必须和 NC-092 的 Caster flags、health/metrics endpoint 对齐。
NC-094 可以保留 dev mock fallback，但真实 AdminService API shape 必须是默认联调目标。
NC-095 可以并行编写矩阵，但必须在 NC-097 前吸收 NC-091 到 NC-094 的最终命令。
```

## 5. PostgreSQL 边界

PostgreSQL 是 Phase 2 的 desired state 和长期审计事实来源。

### 5.1 必需表域

Phase 2 最小闭环需要以下表域：

| 表 | 来源 / 状态 | Phase 2 用途 |
| --- | --- | --- |
| `accounts` | NC-089 migration 已有 | 管理员 actor、audit 关联和后续认证基础。 |
| `access_accounts` | NC-089 migration 已有 | v2 auth projection 输入；完整业务不在 Phase 2 展开。 |
| `hosts` | NC-089 migration 已有 | Host registry 和 agent online/offline 展示。 |
| `agents` | NC-089 migration 已有 | Agent identity、secret hash、heartbeat 状态。 |
| `runtimes` | NC-089 migration 已有 | Runtime registry。 |
| `runtime_desired_states` | NC-089 migration 已有 | desired state 当前版本，Agent polling 的权威输入。 |
| `runtime_actual_snapshots` | NC-089 migration 已有 | actual snapshot 历史和 Web 聚合输入。 |
| `config_versions` | NC-089 migration 已有 | Runtime 配置权威版本。 |
| `config_releases` | NC-089 migration 已有 | 配置发布 / 回滚目标和状态。 |
| `control_intents` | NC-089 migration 已有 | Web intent 的持久化和幂等审计输入。 |
| `operation_audit_logs` | NC-089 migration 已有 | 控制面写操作审计。 |
| `runtime_events` | Phase 2 必须补齐或等价持久化 | Agent runtime-events 的长期记录和 Web event timeline。 |

`runtime_events` 如果不单独建表，NC-091 必须在任务卡和文档中说明等价持久化位置；仅内存保存不能通过 NC-097 闭环 gate。

### 5.2 事务边界

AdminService 写操作必须遵守：

```text
1. 校验 actor、权限、目标资源、expected_version 和业务约束。
2. 在同一 PostgreSQL transaction 中写入 source-of-truth row。
3. 同一 transaction 中写 control_intents / operation_audit_logs。
4. 提交后生成或刷新 Redis projection。
5. 返回 intent accepted / updated desired version，不承诺远端进程已完成。
```

事务内必须保证：

```text
runtime_desired_states.version 单调递增。
runtime_desired_states.generation 可被 Agent 用作 reconcile 代次。
control_intents.request_id + body fingerprint 幂等。
operation_audit_logs 只追加。
config_versions 发布后不可原地修改。
```

禁止：

```text
Agent 直接修改 PostgreSQL 业务表。
Caster 热路径访问 PostgreSQL。
Web 直接访问 PostgreSQL。
Redis 中的 desired/config/auth 数据成为长期唯一事实。
```

## 6. Redis 边界

Redis 在 Phase 2 中只承担：

```text
projection
cache
runtime TTL
pub/sub bus
```

Redis 数据必须可由 PostgreSQL 或 Runtime/Agent 观测重新生成。Redis 丢失不能导致长期业务事实永久丢失。

### 6.1 Phase 2 Redis key registry

| Key / Channel | Type | TTL | Owner | Writer | Reader | 用途 |
| --- | --- | --- | --- | --- | --- | --- |
| `auth:access-account:<username>` | STRING JSON | none | AdminService | projection worker | Caster | AccessAccountAuthIndex。 |
| `auth:policy:<access_account_id>` | STRING JSON | none | AdminService | projection worker | Caster | 访问策略投影。 |
| `auth:version` | STRING integer | none | AdminService | projection worker | Caster | Auth projection 版本。 |
| `config:runtime:<runtime_id>` | STRING JSON | none | AdminService | projection worker | Agent / Caster | Runtime 配置投影。 |
| `control:desired-state:<host_id>` | STRING JSON | none | AdminService | projection worker | Agent | Host 级 desired-state 投影，shape 与 Agent HTTP polling response 一致。 |
| `config:version` | STRING integer | none | AdminService | projection worker | Agent / Caster | 全局配置投影版本。 |
| `control:config` | Pub/Sub JSON | none | AdminService | projection worker | Agent / Caster | Runtime / Host desired projection 变更通知。 |
| `agent:heartbeat:<agent_id>` | STRING JSON | 45s | Agent | Agent | AdminService | Agent heartbeat TTL。 |
| `runtime:actual:<runtime_id>` | STRING JSON | 60s | Agent | Agent | AdminService | Runtime actual 快照 TTL。 |
| `runtime:worker-stat:<runtime_id>` | HASH | 60s | Caster | Caster | AdminService | Worker metrics 快照。 |
| `runtime:mount-owner:<runtime_id>` | HASH | 60s | Caster | Caster | AdminService | mount -> worker owner 快照。 |
| `session:access-account:<access_account_id>` | HASH | 60s | Caster | Caster | AdminService | connect_key -> OnlineSession。 |
| `session:account:<account_id>` | HASH | 60s | Caster | Caster | AdminService | account 维度在线会话。 |
| `session:mount:<mount>` | HASH | 60s | Caster | Caster | AdminService | mount 维度在线会话。 |
| `stream:mount:<mount>` | Pub/Sub binary | none | Caster | Caster | Caster | 跨 Runtime mount 数据流。 |
| `stream:runtime:<runtime_id>` | Pub/Sub JSON | none | Caster | Caster | Agent | Runtime 观测事件。 |
| `control:kick` | Pub/Sub JSON | none | AdminService | projection worker | Caster | kick / policy 变更通知。 |
| `sourcetable:runtime:<runtime_id>` | STRING JSON | 10-30s | Caster | Caster | Caster | 单 Runtime 源列表快照。 |
| `sourcetable:index` | SET 或 STRING JSON | 10-30s | Caster | Caster | Caster | 可选 Runtime 源列表快照索引。 |
| `sourcetable:changed` | Pub/Sub JSON | none | Caster | Caster | Caster | 可选源列表快照变更通知。 |

TTL hash 使用 key-level expiry；Phase 2 不要求依赖 Redis per-field TTL。

禁止：

```text
写旧 ACT:*、MPT:*、STR:*、PULL:*、PUSH:*、CASTER:* 作为 v2 通过证据。
把 Redis projection 写成功当成 PostgreSQL 事务成功的替代。
让 Web 直接拼 Redis key。
让 Agent 或 Caster 依赖 Redis 中的旧 schema。
```

### 6.2 PG -> Redis projection

Projection worker 或同步投影路径必须幂等：

```text
input: PostgreSQL source row + version/checksum
output: Redis projection key
notify: PUBLISH control:config with projection key, runtime_id or host_id, and version
retry: safe
rebuild: full rebuild from PostgreSQL
```

Projection 失败语义：

```text
AdminService 可以先持久化 desired intent，但不得宣称 projection 已完成。
NC-097 闭环 gate 必须使用真实 Redis，并验证投影最终可读且 `control:config` notify 可订阅。
Redis 不可用时，Phase 2 系统 smoke 不能标为通过。
```

## 7. HTTP / JSON Endpoint 边界

Phase 2 使用 HTTP/JSON，不引入 gRPC。

### 7.1 Web / Control API

Phase 2 必需 endpoint：

```text
GET  /api/v1/health

GET  /api/v1/control/hosts
GET  /api/v1/control/hosts/{host_id}

GET  /api/v1/control/runtimes
POST /api/v1/control/runtimes
GET  /api/v1/control/runtimes/{runtime_id}
PUT  /api/v1/control/runtimes/{runtime_id}/desired-state

POST /api/v1/control/runtimes/{runtime_id}/actions/start
POST /api/v1/control/runtimes/{runtime_id}/actions/stop
POST /api/v1/control/runtimes/{runtime_id}/actions/restart
POST /api/v1/control/runtimes/{runtime_id}/actions/drain
POST /api/v1/control/runtimes/{runtime_id}/actions/undrain

GET  /api/v1/control/projection-keys
```

动作类 API 返回 intent receipt：

```json
{
  "intent_id": "intent_01HXYZ",
  "status": "accepted",
  "runtime_id": "rt_01HXYZ",
  "desired_version": 44
}
```

HTTP 202 或 `accepted` 只表示意图已持久化，不表示 Caster 已启动、停止或重启完成。

### 7.2 Agent API

Phase 2 必需 endpoint：

```text
POST /api/v1/agents/register
POST /api/v1/agents/heartbeat
GET  /api/v1/agents/{agent_id}/desired-state?since_version=<version>
POST /api/v1/agents/{agent_id}/runtime-events
POST /api/v1/agents/{agent_id}/runtime-metrics
```

Phase 2 actual snapshot 冻结为通过 `runtime-metrics` 上报：

```json
{
  "agent_id": "ag_01HXYZ",
  "host_id": "host_01HXYZ",
  "actual": [
    {
      "runtime_id": "rt_01HXYZ",
      "host_id": "host_01HXYZ",
      "agent_id": "ag_01HXYZ",
      "actual_state": "running",
      "process_id": 4242,
      "start_token": "start_01HXYZ",
      "config_version": 17,
      "listen_port": 4202,
      "worker_count": 2,
      "connections": 3,
      "mounts": 1,
      "sources": 1,
      "clients": 2,
      "send_bps": 128,
      "recv_bps": 64,
      "loop_delay_ms_p95": 7,
      "redis_connected": true,
      "observed_desired_version": 44,
      "updated_at": "2026-06-27T00:00:01Z",
      "last_error": ""
    }
  ]
}
```

`POST /api/v1/agents/{agent_id}/actual-state` 不属于 Phase 2 必需 endpoint。若后续任务新增该 endpoint，必须同步 OpenAPI、Web client、QA matrix 和本文，且不得与 `runtime-metrics.actual[]` 形成两个并行事实源。

## 8. Desired / Actual 状态机

### 8.1 Desired state

允许值：

```text
running
stopped
draining
deleted
```

必需字段：

```text
runtime_id
host_id
desired_state
config_version
listen_port
worker_count
max_worker_count
restart_policy
draining
version
generation
updated_at
```

Agent 对同一 `runtime_id` 的相同 `version/generation` 必须幂等执行。

### 8.2 Actual state

允许值：

```text
missing
unknown
starting
running
stopping
stopped
failed
```

AdminService 聚合规则：

```text
heartbeat.runtime_summaries 用于快速在线摘要。
runtime-metrics.actual[] 是完整 actual snapshot 的主输入。
Redis runtime:actual:<runtime_id> 是短 TTL operational view。
runtime_actual_snapshots 是长期低频历史。
Web 展示必须标注 updated_at / stale / offline。
```

Web convergence 规则：

| 条件 | Web 状态 |
| --- | --- |
| `actual_state=running` 且 `observed_desired_version >= desired.version` | converged / running |
| desired version 新于 actual observed version | pending / applying |
| Agent heartbeat stale | agent_offline / stale |
| Caster actual failed | failed |
| desired draining 或 actual draining 相关指标存在 | draining |
| actual 缺失但 desired running | starting 或 unknown，不能显示完成 |

## 9. Agent Reconcile 契约

Agent 只管理本机 runtime，只管理由自己创建或登记的进程。

必需行为：

```text
register or resume agent identity。
poll desired-state with since_version。
cache last-known desired state。
render local runtime config。
start navcaster-caster when desired=running and actual missing/stopped。
stop navcaster-caster when desired=stopped/deleted。
collect Caster local health/metrics。
submit runtime-metrics.actual[]。
submit runtime-events。
preserve runtime when AdminService is temporarily unreachable。
after AdminService recovers, upload buffered events and latest actual snapshot。
```

禁止：

```text
AdminService 离线时自行发明新的 desired state。
扫描并杀死其他 NavCaster 进程。
管理 start_token 不匹配的进程。
直接修改 PostgreSQL 业务表。
承载 NTRIP 数据面。
```

进程管理边界：

```text
start_token 是 Agent 识别自己创建的 runtime 的最小凭据。
pid 文件只能作为辅助，不能单独证明所有权。
重复 reconcile 同一 desired generation 不得重复启动进程。
stop 必须先 graceful，再 timeout kill，并上报事件。
restart_policy=never/on_failure/always 必须有可测试分支。
```

## 10. Runtime Events Contract

`POST /api/v1/agents/{agent_id}/runtime-events` payload：

```json
{
  "agent_id": "ag_01HXYZ",
  "host_id": "host_01HXYZ",
  "events": [
    {
      "event_id": "evt_01HXYZ",
      "runtime_id": "rt_01HXYZ",
      "host_id": "host_01HXYZ",
      "agent_id": "ag_01HXYZ",
      "type": "process_started",
      "severity": "info",
      "desired_version": 44,
      "process_id": 4242,
      "message": "runtime started",
      "occurred_at": "2026-06-27T00:00:01Z",
      "metadata": {
        "config_checksum": "sha256:test"
      }
    }
  ]
}
```

Severity：

```text
debug
info
warn
error
fatal
```

Phase 2 required event types：

```text
desired_received
config_rendered
process_started
process_exited
process_restart_scheduled
process_stop_timeout
runtime_health_ok
runtime_health_failed
runtime_metrics_sampled
runtime_reload_succeeded
runtime_reload_failed
agent_offline_buffered
agent_reconnected
```

幂等规则：

```text
event_id 非空时，AdminService 以 (agent_id, event_id) 去重。
event_id 为空时，AdminService 可以接受并生成 ID，但 QA 不用该路径作为幂等通过证据。
older duplicate events 不得覆盖 newer actual snapshot。
```

## 11. Runtime Metrics Contract

`runtime-metrics.actual[]` 是 Phase 2 actual snapshot 主入口。

必需字段：

```text
runtime_id
host_id
agent_id
actual_state
observed_desired_version
updated_at
```

运行进程字段：

```text
process_id
start_token
config_version
config_path
config_checksum
listen_port
worker_count
started_at
last_exit_code
last_error
```

数据面指标：

```text
connections
mounts
sources
clients
send_bps
recv_bps
loop_delay_ms_p95
redis_connected
```

AdminService 处理规则：

```text
同一 runtime_id 只把 updated_at 更新或等价新鲜的 snapshot 作为 current actual。
older snapshot 可保留为历史，但不得覆盖 current view。
runtime_actual_snapshots 保存低频历史；是否每次采样都入库由 NC-091 决定，但 current actual 必须可被 control API 读取。
runtime actual 写 Redis TTL 后必须可由 AdminService 或 Web operational view 读取。
```

## 12. Caster Pub/Sub Contract

Phase 2 跨 Runtime 最小链路使用：

```text
channel: stream:mount:<mount>
payload: raw NTRIP/RTCM data bytes
```

发送路径：

```text
source session -> owner Worker local fan-out -> Redis publish stream:mount:<mount>
```

接收路径：

```text
Redis subscribe stream:mount:<mount> -> subscriber Worker -> local clients
```

Phase 2 最小语义：

```text
单 mount 同一时刻只按一个 active source 处理。
Publisher Runtime 本地 clients 使用 local fan-out，不依赖 Redis echo。
Publisher Runtime 不得把自己发布后收到的 echo 再次 fan-out 给同一批本地 clients。
Remote Runtime clients 可以通过 subscribe 接收 payload。
Pub/Sub 不提供长期持久化、不提供 exactly-once、不作为审计事实。
```

NC-093 如果引入 envelope 或 origin metadata，必须同步 Redis registry、本文和 QA gate。未同步前，Phase 2 gate 只验收 raw payload + echo suppression 的最小链路。

## 13. Web Contract

Web Phase 2 必须：

```text
默认使用真实 AdminService v2 API shape；mock 只能作为 dev fallback。
展示 desired_state 和 actual_state 的差异。
展示 observed_desired_version、desired.version 或等价 convergence 信息。
展示 last_heartbeat_at、actual updated_at 和 stale/offline。
所有 Start/Stop/Restart/Drain/Undrain 只显示 intent accepted / pending，不能显示远端已完成。
actual 收敛后再显示 running/stopped/failed。
保持 v2 管理台风格，不沿用旧 Web 页面作为兼容义务。
```

Web 禁止：

```text
直接访问 Redis。
直接调用 Agent local runtime API。
直接访问 Caster internal session 容器。
把 HTTP 202 / accepted 显示为进程执行成功。
用旧 HTTP API shape 作为 fallback 通过证据。
```

## 14. Phase 2 Done Definition

Phase 2 DEV_DONE / QA / Review 的最低闭环证据：

```text
1. AdminService 真实 PG/Redis 可启动，health 显示 postgres=ok redis=ok。
2. Web 或 curl 创建 Runtime desired state。
3. PostgreSQL runtime_desired_states 和 control_intents 可验证。
4. Redis config/runtime projection 和 `control:config` notify 可验证。
5. Agent 拉取 desired state 并启动真实 navcaster-caster。
6. Caster local health/metrics 可验证。
7. Agent 上报 runtime-metrics.actual[] 和 runtime-events。
8. AdminService control runtime API 返回 desired + actual。
9. Web 展示 desired/actual 收敛或明确 failed/stale。
10. 单 Runtime NTRIP source/client payload smoke 通过。
11. 双 Runtime Redis Pub/Sub payload smoke 通过，或 NC-093 明确阻断且 NC-097 不写 QA_PASSED。
```

不满足上述闭环时：

```text
功能性失败 -> QA_FAILED。
环境缺失且无法替代 -> QA_BLOCKED。
只覆盖部分边界 -> QA_PARTIAL。
不得用旧系统兼容性补充为 QA_PASSED。
```

## 15. 后续更新规则

以下变更必须同步本文、`doc/qa/v2-phase2-contract-qa-gate.md`、OpenAPI、任务卡和相关岗位记忆：

```text
新增或删除 Phase 2 HTTP endpoint。
改变 runtime-metrics.actual[] 或 runtime-events payload shape。
改变 Redis key/channel/payload 或 TTL。
改变 PG table/transaction source-of-truth 边界。
改变 Agent reconcile ownership 或 start_token 语义。
改变 Web intent accepted / actual convergence 展示规则。
把容量基线升级为发布硬门槛。
```
