# NavCaster v2 Control Plane / Agent / Runtime Contract

更新时间：2026-06-27
任务：NC-081 v2 Architecture API Data Contract
状态：v2 冻结契约
适用范围：AdminService、Agent、Caster Runtime 的 desired / actual state、配置版本、离线自治、执行语义和审计。
可信度：架构冻结文档；后续 AdminService、Agent、Caster Runtime MVP 必须按本文实现。

## 1. 契约结论

控制面执行链路冻结为：

```text
Web -> AdminService -> PostgreSQL desired state -> Agent polling/reconcile
Agent -> local process action -> Caster Runtime
Agent / Caster Runtime -> actual state -> AdminService -> Web
```

核心约束：

```text
AdminService 只写全局意图，不直接远程执行命令。
Agent 只管理本机资源，不做全局调度。
Caster Runtime 不依赖 AdminService 存活。
AdminService 离线时 Agent 使用 last-known desired state 继续守护本机 Runtime。
动作 API 返回 intent accepted，不表示 Runtime 已经完成动作。
desired state 和 actual state 必须用版本、时间和状态机显式区分。
```

## 2. 状态对象

### 2.1 DesiredState

DesiredState 是 PostgreSQL 中的权威目标状态。

```json
{
  "runtime_id": "rt_01HXYZ",
  "host_id": "host_01HXYZ",
  "desired_state": "running",
  "config_version": 17,
  "listen_port": 4202,
  "worker_count": 4,
  "max_worker_count": 16,
  "restart_policy": "on_failure",
  "draining": false,
  "version": 42,
  "generation": 42,
  "updated_by": "acc_admin",
  "updated_at": "2026-06-27T10:00:00Z"
}
```

字段语义：

| 字段 | 语义 |
| --- | --- |
| `runtime_id` | Runtime 全局 ID。 |
| `host_id` | Runtime 所属 Host。 |
| `desired_state` | `running` / `stopped` / `draining` / `deleted`。 |
| `config_version` | 目标配置版本。 |
| `listen_port` | Runtime 对外 NTRIP 端口。变更通常需要重启。 |
| `worker_count` | 目标 Worker 数。增加可在线执行，降低走 draining。 |
| `max_worker_count` | 管理上限。 |
| `restart_policy` | `never` / `on_failure` / `always`。 |
| `draining` | Runtime 级别是否停止接收新 mount / 新连接。 |
| `version` | desired state 单调递增版本。 |
| `generation` | Agent reconcile 使用的目标代次，等同或派生自 version。 |

### 2.2 ActualState

ActualState 是 Agent 和 Runtime 观测到的实际状态。

```json
{
  "runtime_id": "rt_01HXYZ",
  "host_id": "host_01HXYZ",
  "agent_id": "ag_01HXYZ",
  "actual_state": "running",
  "process_id": 12345,
  "start_token": "start_01HXYZ",
  "config_version": 17,
  "listen_port": 4202,
  "worker_count": 4,
  "connections": 16000,
  "mounts": 4000,
  "sources": 4000,
  "clients": 12000,
  "send_bps": 123456,
  "recv_bps": 23456,
  "loop_delay_ms_p95": 12,
  "redis_connected": true,
  "last_error": "",
  "observed_desired_version": 42,
  "updated_at": "2026-06-27T10:00:05Z"
}
```

字段语义：

| 字段 | 语义 |
| --- | --- |
| `actual_state` | `missing` / `starting` / `running` / `stopping` / `stopped` / `failed` / `unknown`。 |
| `process_id` | 本机进程 ID。 |
| `start_token` | Agent 启动该进程时写入的本机识别 token。 |
| `observed_desired_version` | Agent 当前已尝试执行的 desired 版本。 |
| `updated_at` | Agent 观测时间。 |

### 2.3 ConfigVersion

ConfigVersion 是 PG 权威配置版本。

```json
{
  "config_version": 17,
  "status": "published",
  "payload": {},
  "checksum": "sha256:...",
  "created_by": "acc_admin",
  "created_at": "2026-06-27T09:50:00Z",
  "published_at": "2026-06-27T09:55:00Z"
}
```

状态：

```text
draft
published
archived
rolled_back
```

配置来源顺序：

```text
PostgreSQL ConfigVersion = 权威配置。
Redis CONF projection = Runtime 快速读取投影。
Agent local config file = 启动快照。
Caster memory config = 实际运行中配置。
```

## 3. AdminService 工作流

### 3.1 创建 Runtime

```text
1. Web 调用 POST /api/v1/control/runtimes。
2. AdminService 校验权限、Host 在线状态、端口冲突、worker_count 上限。
3. AdminService 写 runtimes 和 runtime_desired_states。
4. AdminService 写 operation_audit_logs。
5. AdminService 返回 intent accepted。
6. Agent 拉取 desired state 后执行本机启动。
7. Agent 上报 actual state。
8. Web 通过 polling 或 SSE 看到 running / failed。
```

API 返回只承诺：

```text
AdminService 已接受并持久化意图。
不承诺进程已经启动。
```

### 3.2 启动 / 停止 / 重启

动作模型：

| 操作 | DesiredState 变化 | Agent 动作 |
| --- | --- | --- |
| start | `desired_state=running` | 渲染配置并启动进程。 |
| stop | `desired_state=stopped` | 优雅停止，超时强制停止。 |
| restart | 写 runtime action intent 或 bump generation | 先 stop 再 start，保留 desired running。 |
| drain | `draining=true` | 调用 Runtime local API 或重启到 draining 配置。 |
| undrain | `draining=false` | 允许接收新 mount / 新连接。 |

### 3.3 配置发布

```text
1. 管理员创建或修改 config draft。
2. AdminService 写 config_versions(status=draft)。
3. 管理员发布。
4. AdminService 写 config_versions(status=published) 和 config_releases。
5. AdminService 生成 Redis config projection。
6. AdminService 更新目标 Runtime desired_state.config_version。
7. Agent 拉取新版本并判断 reload 或 restart。
8. Agent 上报执行结果。
```

热更新边界：

| 类型 | 策略 |
| --- | --- |
| 账号投影、访问控制、Source table 可见性、采样间隔 | 可热更新。 |
| listen_port、Redis endpoint、worker_count 降低、event_base 初始化参数 | 需要重启或 draining。 |

## 4. Agent Reconcile

Agent 主循环：

```text
load local state
register or resume agent identity
start heartbeat loop
start desired-state polling loop
start runtime supervisor loop
start local metrics loop
```

Reconcile 规则：

```text
actual missing and desired=running
  -> render config
  -> start navcaster-caster
  -> save start_token and actual snapshot

actual running and desired=stopped
  -> graceful stop
  -> timeout kill
  -> update actual stopped

actual running and config_version mismatch
  -> local reload if allowed
  -> otherwise restart

actual running and worker_count increased
  -> local Runtime API add worker if supported
  -> otherwise restart only if contract allows

actual running and worker_count decreased
  -> set draining on target workers
  -> stop workers only after no active sessions

process exited unexpectedly
  -> apply restart_policy

AdminService unreachable
  -> keep last-known desired state
  -> continue heartbeat retry
  -> continue local supervisor
```

Agent 不得：

```text
在 AdminService 离线时自行发明新的 desired state。
管理不是自己启动或不匹配 start_token 的进程。
扫描并杀死其他 NavCaster 进程。
修改 PostgreSQL 权威业务数据。
```

## 5. Agent 本地状态

第一版使用文件存储，不引入嵌入式数据库。

```text
agent_state.json
  agent_id
  host_id
  agent_secret_ref
  bootstrap_status
  last_desired_version
  last_admin_endpoint

runtimes/<runtime_id>/desired.json
runtimes/<runtime_id>/actual.json
runtimes/<runtime_id>/config.yml
runtimes/<runtime_id>/events.log
runtimes/<runtime_id>/pid
runtimes/<runtime_id>/start_token
```

最小缓存字段：

```text
runtime_id
config_version
desired_state
worker_count
listen_port
restart_policy
last_successful_config
```

本地状态用途：

```text
AdminService 离线时继续守护。
进程重启后恢复 Agent identity。
避免误杀非本 Agent 管理的进程。
支持故障后上传补偿事件。
```

## 6. 离线自治

### 6.1 AdminService 离线

Agent 行为：

```text
继续运行本机 supervisor。
继续按 last-known desired state 保持 Runtime。
Runtime 崩溃后按 restart_policy 拉起。
本地追加 runtime events。
周期性重试连接 AdminService。
恢复后上传 actual state、events 和 metrics。
```

Agent 禁止：

```text
自行停止 Runtime。
自行创建新 Runtime。
自行变更 config_version。
自行执行未缓存过的控制面动作。
```

Caster Runtime 行为：

```text
继续服务已有和新 NTRIP 连接。
继续使用本地 cache 和 Redis 投影。
继续写 Redis runtime state。
不依赖 AdminService 存活。
```

### 6.2 Agent 离线

AdminService 行为：

```text
heartbeat timeout 后标记 host.agent_status=offline。
Web 展示 agent_offline。
阻止该 host 的新动作，或写入 pending 但明确提示不能确认执行。
不假设 Runtime 已停止。
不直接远程执行命令。
```

### 6.3 Caster Runtime 离线

Agent 行为：

```text
识别进程退出。
记录 exit code 和 stderr 摘要。
按 restart_policy 决定是否重启。
上报 crash / restart event。
```

AdminService 行为：

```text
展示 actual_state=failed / restarting / running。
保留 desired state。
审计 runtime event。
```

## 7. 审计和事件

必须审计的控制面操作：

```text
Runtime create / delete。
desired_state start / stop / restart / drain / undrain。
worker_count change。
config version create / publish / rollback。
Host enable / disable。
Agent bootstrap token create / revoke。
权限、账号、接入账号和计费相关写操作。
```

Runtime event 最小字段：

```json
{
  "event_id": "evt_01HXYZ",
  "runtime_id": "rt_01HXYZ",
  "host_id": "host_01HXYZ",
  "agent_id": "ag_01HXYZ",
  "type": "process_started",
  "severity": "info",
  "desired_version": 42,
  "process_id": 12345,
  "message": "runtime started",
  "occurred_at": "2026-06-27T10:00:03Z"
}
```

事件类型：

```text
desired_received
config_rendered
process_started
process_exited
process_restart_scheduled
process_stop_timeout
runtime_health_failed
runtime_reload_succeeded
runtime_reload_failed
agent_offline_buffered
agent_reconnected
```

## 8. API 状态语义

动作类 API 统一返回：

```json
{
  "request_id": "req_01HXYZ",
  "intent_id": "intent_01HXYZ",
  "status": "accepted",
  "desired_version": 43,
  "runtime_id": "rt_01HXYZ"
}
```

错误语义：

| HTTP | 场景 |
| --- | --- |
| 400 | 参数无效或状态转换非法。 |
| 401 | 未登录或 Agent 认证失败。 |
| 403 | 权限不足。 |
| 404 | 目标不存在。 |
| 409 | 版本冲突、端口冲突、状态冲突。 |
| 422 | 业务约束不满足，例如 worker_count 超上限。 |
| 503 | 控制面依赖不可用，例如 PG/Redis 不可用。 |

## 9. 版本和幂等

所有写操作应具备：

```text
request_id
actor_id
expected_version 可选
desired_version 单调递增
audit record
```

幂等规则：

```text
同一 request_id 重放，若 body fingerprint 相同，返回第一次结果。
同一 request_id 重放，若 body fingerprint 不同，返回 409。
未带 expected_version 的覆盖写只允许用于明确幂等动作。
涉及配置发布、worker_count 调整和 restart 的动作必须可审计。
```

## 10. 后续验证要求

后续实现任务至少覆盖：

```text
AdminService 写 desired 后 API 只返回 accepted。
Agent polling 获取 desired version。
Agent 离线期间 Runtime 继续运行。
AdminService 离线期间 Agent 继续守护并本地记录事件。
AdminService 恢复后 Agent 上传 buffered events。
restart_policy=never/on_failure/always 三种路径。
worker_count 增加不迁移已有连接。
worker_count 降低进入 draining。
Agent 不管理 start_token 不匹配的进程。
```
