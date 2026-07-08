# NavCaster v2 Phase 3 Production Contract and Delivery Gate

更新时间：2026-06-27
任务：NC-100 v2 Phase 3 Production Contract And Delivery Gate
来源基线：`team-dev @ 1044bbe`
状态：Phase 3 冻结契约，作为 NC-101 到 NC-107 的输入。
适用范围：NavCaster v2 第三轮生产化闭环加固、运行边界、容量口径、交付 gate 和审查证据。
可信度：架构 / 文档准入契约；不表示产品代码已全部实现，不表示生产发布批准。

相关岗位：

```text
architect
docs-planner
backend-http
backend-core
frontend
qa
reviewer
integration
```

## 1. 冻结结论

Phase 3 从 Phase 2 真实闭环基线继续推进：

```text
Web intent
  -> AdminService PostgreSQL desired/source-of-truth
  -> AdminService Redis projection/cache/bus
  -> Agent desired polling / reconcile
  -> Agent supervises real navcaster-caster
  -> Caster multi-worker NTRIP / Redis Pub/Sub data path
  -> Agent runtime-events / runtime-metrics / actual snapshots
  -> AdminService actual aggregation / lifecycle tracking
  -> Web desired / actual / events / metrics production smoke
```

Phase 3 只按 v2 契约验收：

```text
不兼容旧架构。
不兼容旧 HTTP API。
不兼容旧 Redis key。
不兼容旧 protobuf。
不兼容旧命名规则。
不兼容旧 Web 风格。
不以旧 CasterService、旧 route、旧 Redis schema 或 mock UI 作为通过证据。
```

Phase 3 的交付结论是“可作为后续演进基线”，不是生产发布批准：

```text
不承诺生产发布。
不承诺 16k 正式容量压测。
不承诺跨区域或多副本 AdminService HA。
不扩展到账户、计费、支付、供应商结算等非本轮控制面目标。
不把轻量容量基线写成生产容量指标。
```

## 2. Phase 2 输入基线

Phase 3 继承的已验证事实：

```text
基线：team-dev @ 1044bbe
来源：merge NC-097 v2 phase2 closed loop
Review：NC-098 APPROVED_WITH_NOTED_RISKS
QA：NC-097 QA_PASSED_WITH_NOTED_RISKS
```

Phase 2 已通过：

```text
真实 PostgreSQL + Redis Admin gate。
Redis desired projection 和 v2:control:config notify。
Agent 从 Admin desired state 启动真实 navcaster-caster。
Caster health/metrics。
双 Runtime Redis Pub/Sub payload round trip。
Web live browser smoke。
Web action intent 写入 PostgreSQL desired/control_intents 并更新 Redis projection。
统一 Web intent -> Agent -> Caster -> Admin actual -> Web convergence smoke。
```

Phase 2 保留到 Phase 3 的风险：

```text
轻量容量基线未运行。
Windows 环境中 422xx 端口长跑 acceptor bind 失败；Phase 2 使用低端口完成真实 gate。
生产部署、长期 soak、16k 容量、跨物理主机网络异常不是 Phase 2 批准内容。
```

## 3. Phase 3 范围

### 3.1 In Scope

Phase 3 必须完成或形成可审查证据：

```text
AdminService 控制指令生命周期：accepted、projected、observed、converged、superseded、failed、stale。
AdminService PG source-of-truth 与 Redis projection/cache/bus 的一致性检查和可观测状态。
Runtime desired/actual 查询能支撑 Web 和 QA 判断 pending、converged、failed、stale。
Agent 多 Runtime reconcile、真实 Caster 进程监督、异常恢复和 actual/events/metrics 上报。
Caster 多 Worker 分片稳定性、mount ownership、local fan-out、remote Pub/Sub fan-out counters。
Redis Pub/Sub 轻量容量基线：runtime、worker、source/client、payload、counter、CPU/RSS。
Web 真实 AdminService API 浏览器 smoke：hosts、runtimes、runtime detail、action intent、events/metrics。
真实 PostgreSQL + Redis fixture 下的系统级一致性检查。
NC-101 到 NC-107 的证据链、集成顺序、QA gate 和 Reviewer 输入。
```

### 3.2 Out of Scope

Phase 3 明确不做：

```text
旧系统兼容、旧 HTTP API fallback、旧 Redis key fallback、旧 protobuf 兼容层。
旧 Web 页面或旧命名规则兼容。
生产发布、生产变更审批、安全渗透审计、跨区域灾备。
正式 16k 容量压测、长时间 soak 或 SLA 承诺。
AdminService 多副本 HA、leader election、无状态 round-robin 写入口池。
账务、订阅、支付、供应商结算和非 v2 控制面业务扩展。
```

## 4. 四类交付门槛

### 4.1 可持续运行门槛

Phase 3 必须证明系统不会只在一次 happy path 中可用：

```text
Agent 在 AdminService 短暂不可用时保持 last-known desired state，不停止已运行 Caster。
Agent 只管理本机且 start_token 匹配的 Runtime 进程。
Caster health/metrics 不依赖 AdminService 存活。
Caster 热路径不访问 PostgreSQL，不同步调用 AdminService。
Agent/Caster 异常恢复有 runtime-events 和 actual snapshots。
Web 对 stale/offline/failed/pending 有明确展示，不把 accepted 当作完成。
测试结束后清理进程、端口、PG fixture、Redis v2 测试 key 和临时目录。
```

最低证据：

```text
AdminService stop/recover mini-smoke。
Agent stop/recover 或 stale/offline mini-smoke。
Caster kill/restart_policy mini-smoke。
多 Runtime start/stop/restart/drain/no-op smoke。
```

### 4.2 可观测门槛

Phase 3 必须能解释“发生了什么、谁认为完成、还有什么没收敛”：

```text
control_intents 可审计，状态和 desired_version 可追踪。
runtime_events 可按 runtime/agent/host 查询或被 QA 追溯。
runtime_actual_snapshots 或 current actual view 可读，且 older snapshot 不覆盖 newer current view。
Redis projection 写入、notify、stale/error 状态可观测。
Caster metrics 可区分 local fan-out、Redis publish、Redis subscribe、remote fan-out 和 error counters。
Web 显示 desired_state、actual_state、observed_desired_version、updated_at、stale/offline、events/metrics。
```

最低证据：

```text
SQL 检查 desired/control_intents/runtime_events/runtime_actual_snapshots。
redis-cli 检查 v2:config:runtime、v2:control:desired-state、v2:control:config notify、runtime TTL。
AdminService control API 返回 desired + actual + lifecycle status。
Caster /metrics 输出 worker/pubsub/fan-out counters。
Web 浏览器 smoke 记录 console error count 和页面状态。
```

### 4.3 可验证门槛

Phase 3 必须可在 QA/integration worktree 复现：

```text
所有 gate 有命令、端口、fixture、通过标准和失败分级。
真实 PG/Redis 不可用时不能写系统闭环通过。
Web 浏览器 smoke 不能只用 mock 数据。
Redis Pub/Sub gate 必须使用真实 Redis fixture。
容量基线必须记录规模、环境、指标、可信度和未达项。
未运行项必须列明原因、风险、后续任务和是否阻断。
```

最低证据：

```text
git diff --check。
admin go test/build/selfcheck 或等价命令。
agent go test/build。
CMake Ninja navcaster-caster build/self-test。
单 Runtime NTRIP smoke。
双 Runtime Redis Pub/Sub smoke。
Web npm run build。
真实浏览器 smoke。
PG/Redis 一致性检查。
```

### 4.4 可交付门槛

Phase 3 交付基线必须让 Reviewer 能判断是否可进入后续演进：

```text
NC-100 到 NC-106 的 diff、QA 记录、未运行项和风险闭环齐全。
NC-106 形成统一集成 commit 和 _team/qa 记录。
NC-107 输出 findings、open questions、QA 覆盖匹配度和 APPROVED / CHANGES_REQUESTED / BLOCKED。
所有已知环境缺口和容量限制不能被写成生产能力。
若批准，只能批准为 Phase 3 delivery baseline，不代表生产发布。
```

## 5. 模块集成边界

### 5.1 AdminService

AdminService 是 v2 控制面权威入口：

```text
写 PostgreSQL source-of-truth。
生成 Redis projection/cache/bus。
接收 Agent heartbeat、runtime-events、runtime-metrics.actual[]。
聚合 desired/actual/current lifecycle status 供 Web 读取。
写 control_intents 和 operation_audit_logs。
```

AdminService 禁止：

```text
直接远程执行 Agent/Caster 命令。
访问 Caster 热路径。
把 Redis 当成长期唯一事实。
使用旧 HTTP route 或旧 Redis key 作为 v2 通过证据。
```

### 5.2 Agent

Agent 是本机 Runtime 执行者：

```text
register/resume agent identity。
heartbeat and desired polling。
多 Runtime reconcile。
渲染本机 Caster config。
启动/停止/重启/守护 start_token 匹配的 navcaster-caster。
采集 Caster health/metrics。
上报 runtime-events 和 runtime-metrics.actual[]。
AdminService 不可用时保持 last-known desired state。
```

Agent 禁止：

```text
直接修改 PostgreSQL 业务表。
管理其他机器 Runtime。
扫描并杀死非本 Agent 管理进程。
在 AdminService 离线时自行发明新 desired state。
承载 NTRIP 数据面。
```

### 5.3 Caster

Caster 是实时数据面 Runtime：

```text
单进程多 Worker。
Worker 独占 event_base、session map、bufferevent、Redis async contexts。
Acceptor 只做轻量 header parse 和 fd handoff。
mount ownership 稳定，已有连接不跨 Worker 迁移。
source -> local fan-out -> Redis v2 Pub/Sub。
Redis Pub/Sub -> subscriber Worker -> local clients。
暴露 local health/metrics 给 Agent。
```

Caster 禁止：

```text
热路径访问 PostgreSQL。
同步调用 AdminService 保持数据面。
跨 Worker 共享 session map 或 bufferevent。
共享 hiredis async context。
使用旧 MPT:* channel 或旧 key 作为 v2 数据面证据。
```

### 5.4 Web

Web 是控制面操作和观测界面：

```text
默认连接真实 AdminService v2 API。
展示 Host、Runtime、Runtime detail、desired/actual convergence、events 和 metrics。
动作只提交 intent，先展示 accepted/pending/applying。
actual 收敛后再展示 running/stopped/failed。
明确展示 stale/offline/failed。
```

Web 禁止：

```text
直接访问 Redis。
直接调用 Agent local runtime API 或 Caster internal endpoint。
把 mock detail/events/workers 作为 live 事实。
把 HTTP 202/accepted 显示为远端执行完成。
沿用旧 Web 风格或旧 API fallback 作为通过证据。
```

## 6. PG / Redis 一致性口径

PostgreSQL 是 source-of-truth。Redis 只承担：

```text
projection
cache
runtime TTL
pub/sub bus
```

### 6.1 必查 PostgreSQL 表域

Phase 3 系统 gate 至少检查：

```text
hosts
agents
runtimes
runtime_desired_states
runtime_actual_snapshots
runtime_events
config_versions
config_releases
control_intents
operation_audit_logs
```

检查口径：

```text
runtime_desired_states.version 单调递增。
control_intents 记录 action、status、desired_version、request_id 或等价幂等键。
operation_audit_logs 对控制面写操作只追加。
runtime_events 可追溯 process/health/reconcile 事件。
runtime_actual_snapshots 或 current actual view 不被旧 snapshot 覆盖。
```

### 6.2 必查 Redis key/channel

Phase 3 系统 gate 至少检查：

| Key / Channel | Type | 期望 |
| --- | --- | --- |
| `v2:config:runtime:<runtime_id>` | STRING JSON | desired/config projection，带 runtime_id/version/checksum 或等价字段。 |
| `v2:control:desired-state:<host_id>` | STRING JSON | Agent polling 等价 desired projection。 |
| `v2:control:config` | Pub/Sub JSON | projection notify，包含 projection key、host_id 或 runtime_id、version。 |
| `v2:agent:heartbeat:<agent_id>` | STRING JSON + TTL | Agent 在线 TTL。 |
| `v2:runtime:actual:<runtime_id>` | STRING JSON + TTL | Runtime actual operational view。 |
| `v2:runtime:worker-stat:<runtime_id>` | HASH + TTL | worker metrics snapshot。 |
| `v2:runtime:mount-owner:<runtime_id>` | HASH + TTL | mount -> worker owner。 |
| `stream:mount:<mount>` | Pub/Sub binary | NCV2BUS1 envelope + raw bytes。 |

一致性通过标准：

```text
PG desired 写入成功后 Redis projection 最终可读。
Redis projection version 不得新于不存在的 PG desired version。
订阅 v2:control:config 后，desired/config 更新能收到 notify。
删除 projection 后可由 PG rebuild，或明确记录为阻断缺口。
runtime TTL 丢失只影响 operational view，不丢失 PG source-of-truth。
旧 ACT:*、MPT:*、STR:*、PULL:*、PUSH:*、CASTER:* 不作为 v2 通过证据。
```

## 7. 控制指令生命周期

Phase 3 冻结的生命周期语义：

| 状态 | Owner | 语义 |
| --- | --- | --- |
| `accepted` | AdminService | intent 已校验、持久化，desired_version 已生成。 |
| `projected` | AdminService | PG desired 已投影到 Redis，notify 已发布或可重试。 |
| `observed` | Agent | Agent 拉取到该 desired_version，并开始 reconcile。 |
| `applied` | Agent | Agent 已执行本机动作或确认 no-op。 |
| `converged` | AdminService | current actual observed_desired_version 达到 desired_version 且 actual_state 符合目标。 |
| `superseded` | AdminService | 新 desired_version 覆盖旧 intent，旧 intent 不再等待收敛。 |
| `failed` | Agent/AdminService | 执行失败、依赖失败或状态机不可达，有 runtime_event 和 last_error。 |
| `stale` | AdminService | Agent/actual 超过 TTL 或 heartbeat 超时，不能证明执行完成。 |

要求：

```text
动作 API 返回 accepted，不表示远端已完成。
projected 不等于 observed。
observed 不等于 converged。
older actual snapshot 不得把 newer converged/failed current view 回滚。
Web 必须显示 pending/applying/stale/failed，不得把 accepted 显示为完成。
Reviewer 检查生命周期字段是否能从 PG、Redis、Admin API 和 Web 证据串起来。
```

## 8. Runtime events / actual / metrics 契约

### 8.1 Runtime events

Phase 3 必需事件族：

```text
desired_received
desired_applied
desired_superseded
config_rendered
process_started
process_stopped
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

事件最低字段：

```text
event_id
runtime_id
host_id
agent_id
type
severity
desired_version
process_id
occurred_at
message
metadata
```

幂等：

```text
AdminService 以 (agent_id, event_id) 去重。
payload 中 host_id/agent_id 必须绑定或校验请求级 identity。
older duplicate event 不得覆盖 current actual。
```

### 8.2 Actual snapshots

`runtime-metrics.actual[]` 继续作为 Phase 3 actual snapshot 主入口，除非 NC-101 明确同步 OpenAPI、Web types、QA 和本文。

最低字段：

```text
runtime_id
host_id
agent_id
actual_state
observed_desired_version
updated_at
process_id
start_token
config_version
listen_port
worker_count
redis_connected
last_error
```

处理规则：

```text
updated_at 更新或等价新鲜的 snapshot 才能更新 current actual。
older snapshot 可留作历史，但不得覆盖 current actual。
actual_state=running 且 observed_desired_version >= desired.version 才能标为 converged/running。
Agent heartbeat stale 时 Web 不能显示执行完成。
```

### 8.3 Metrics

Phase 3 metrics 必须支持容量和运行态判断：

```text
connection_count
source_count
client_count
mount_count
worker_count
send_bps
recv_bps
loop_delay_ms_p95
fanout_write_count
redis_publish_count
redis_subscribe_message_count
redis_remote_fanout_write_count
redis_error_count
slow_client_disconnect_count
process CPU/RSS 或 QA 采样命令
```

## 9. Caster Worker / Pub/Sub 容量基线口径

Phase 3 容量基线是轻量、可复现、可比较的指标口径，不是正式生产压测。

### 9.1 分片稳定性

必查行为：

```text
worker_count=1/2/4 或当前环境可支持的等价梯度。
同 mount source/client 优先落到 owner Worker。
mount owner 在已有连接期间稳定。
新增 Worker 后新 mount 可进入新 Worker。
draining Worker 不接收新 mount。
已有连接不跨 Worker 迁移。
local fan-out 与 Redis remote fan-out counter 可区分。
Worker 不跨线程共享 session map、bufferevent 或 Redis async context。
```

### 9.2 Redis Pub/Sub 基线

必查行为：

```text
Runtime A source -> Runtime A local fan-out。
Runtime A publish stream:mount:<mount>。
Runtime B subscribe -> remote fan-out -> Runtime B client。
payload 使用 NCV2BUS1 envelope + raw bytes。
subscriber 忽略 origin_runtime_id 等于本 runtime 的 echo。
publish/subscribe/remote fan-out counters 递增且 error=0。
停 Runtime B 不影响 Runtime A 本地 source/client。
```

### 9.3 必报规模和指标

最低建议规模：

```text
runtime_count: 1 and 2
worker_count: 1, 2, 4 if environment allows
source_count: 1, 10, 100 if environment allows
client_count: 1, 30, 300 if environment allows
duration: 2 min smoke, 10 min baseline if stable
payload: deterministic bytes, record size and rate
ports: prefer known-good low ports when Windows 422xx bind issue is present
```

必报指标：

```text
runtime_count
worker_count
source_count
client_count
mount_count
payload_size
payload_rate
fanout_write_count / per_sec
redis_publish_count / per_sec
redis_subscribe_message_count / per_sec
redis_remote_fanout_write_count / per_sec
redis_error_count
send_bps / recv_bps
loop_delay_ms_p95
slow_client_disconnect_count
CPU sampling method and result
RSS sampling method and result
last stable scale
failure point or none
```

不达规模时：

```text
记录最后稳定规模。
记录失败点、端口、日志和瓶颈假设。
不得写成生产容量通过。
如果轻量规模出现崩溃、payload mismatch、跨 mount 串流或 Redis error 持续增长，应阻断 NC-106/NC-107。
```

## 10. NC-101 到 NC-107 依赖和集成顺序

| 顺序 | 任务 | 角色 | 输入 | 必须产出 |
| --- | --- | --- | --- | --- |
| 1 | NC-100 | architect / docs-planner | `team-dev @ 1044bbe` | 本文和 Phase 3 QA gate，冻结范围、边界、容量口径和交付准入。 |
| 2 | NC-101 | backend-http | NC-100 | AdminService control lifecycle、PG/Redis consistency、actual/events aggregation、OpenAPI/types/doc sync。 |
| 3 | NC-102 | backend-core | NC-100 + NC-101 API shape | Agent 多 Runtime reconcile、真实 Caster supervisor、actual/events/metrics、异常恢复 smoke。 |
| 4 | NC-103 | backend-core / qa | NC-100 + NC-102 runtime assumptions | Caster worker partition stability、Redis Pub/Sub capacity baseline script/record。 |
| 5 | NC-104 | frontend | NC-100 + NC-101 API shape | Web live API browser smoke、desired/actual/events/metrics/action intent UI。 |
| 6 | NC-105 | qa | NC-100 到 NC-104 outputs | Phase 3 QA matrix、命令、fixture、失败分级、NC-106 执行清单。 |
| 7 | NC-106 | integration / qa | NC-100 到 NC-105 DEV_DONE | 合入集成、真实系统 smoke、PG/Redis consistency、capacity record、_team/qa 记录。 |
| 8 | NC-107 | reviewer | NC-106 QA_PASSED 或 QA_PASSED_WITH_NOTED_RISKS | 总审查记录、findings、open questions、delivery baseline 结论。 |

并行规则：

```text
NC-101 与 NC-102 可并行，但 NC-102 DEV_DONE 前必须对齐 NC-101 的 actual/events/control lifecycle shape。
NC-103 可独立验证 Caster 数据面，但 NC-106 前必须使用 NC-102/Agent 可启动的 Caster 参数和低端口建议。
NC-104 可使用 seeded AdminService fixture 早跑浏览器 smoke，但 NC-106 必须对真实集成环境复跑。
NC-105 可以并行编写矩阵，但必须吸收 NC-101 到 NC-104 的最终命令和缺口。
NC-106 不得强行合入未 DEV_DONE 的任务。
NC-107 不直接修复代码；阻断项以 CHANGES_REQUESTED 返回总控。
```

## 11. QA / Reviewer 证据要求

每个任务必须记录：

```text
worktree
branch / commit
source baseline
date
environment
commands
results
unrun items
risk classification
whether old API/key/protobuf/Web evidence was excluded
```

NC-106 系统 gate 的最低证据：

```text
真实 Web 浏览器 smoke。
Admin/Agent/Caster 多 Runtime 联调。
PG desired/control_intents/runtime_events/runtime_actual_snapshots SQL evidence。
Redis projection/cache/bus/TTL/pubsub evidence。
控制指令 accepted -> projected -> observed -> converged 或 failed/stale evidence。
Runtime events / actual snapshots / metrics evidence。
Caster worker partition and Redis Pub/Sub capacity record。
Admin/Agent/Caster/Web build/self-test evidence or clear environment gap.
```

NC-107 Reviewer 最低抽查：

```text
NC-100 到 NC-106 diff 是否符合岗位边界。
AdminService 是否真正使用 PG source-of-truth。
Redis key/channel 是否只用 v2 契约证据。
Agent 是否只管理本机 start_token 匹配进程。
Caster 热路径是否访问 PG/AdminService。
Caster Worker 是否跨线程共享 session/bufferevent/map。
runtime-events、actual snapshots、metrics shape 是否和文档一致。
Web 是否把 accepted 和 actual convergence 分开展示。
QA 未运行项是否合理标为阻断或非阻断。
容量基线是否被误写成生产容量承诺。
```

## 12. 后续更新规则

以下变更必须同步本文、`doc/qa/v2-phase3-delivery-qa-gate.md`、OpenAPI、任务卡和相关岗位记忆：

```text
新增、删除或改变 Phase 3 HTTP endpoint。
改变 control intent lifecycle 状态或收敛语义。
改变 runtime-events、runtime-metrics.actual[] 或 metrics payload shape。
改变 PG table/transaction/source-of-truth 边界。
改变 Redis key/channel/payload/TTL 或 Pub/Sub envelope。
改变 Agent reconcile ownership、start_token 或离线自治语义。
改变 Caster worker ownership、mount ownership 或 Pub/Sub echo suppression 语义。
改变 Web intent accepted / pending / actual convergence 展示规则。
把轻量容量基线升级为生产发布或 16k 正式压测门槛。
```
