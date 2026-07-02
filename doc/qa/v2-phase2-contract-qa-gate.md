# NavCaster v2 Phase 2 Contract QA Gate

更新时间：2026-06-27
任务：NC-090 v2 Phase 2 Scope And Contract Gate
来源基线：`feature/NC-089-v2-foundation-closed-loop-integration @ 9497a97`
状态：Phase 2 QA gate 冻结输入，供 NC-095、NC-097、NC-098 细化和执行。
适用范围：v2 Phase 2 真实控制面闭环、PG/Redis、Admin-Agent-Caster-Web smoke、运行态事件/指标和未运行项记录。
可信度：QA 准入契约；不表示本文命令已在 NC-090 执行。

## 1. Gate 结论

Phase 2 QA 只按 v2 契约判定：

```text
不使用旧 HTTP API 作为通过证据。
不使用旧 Redis key 作为通过证据。
不使用旧 protobuf 作为通过证据。
不使用旧 Web 页面或旧命名作为通过证据。
不把 skeleton / mock / not_configured 状态写成真实闭环通过。
```

Phase 2 最终闭环 gate 必须覆盖：

```text
真实 PostgreSQL。
真实 Redis。
AdminService v2 HTTP/JSON。
Agent desired polling and reconcile。
Agent 启动真实 navcaster-caster。
Caster health/metrics。
单 Runtime NTRIP source/client。
双 Runtime Redis Pub/Sub。
Agent runtime-events / runtime-metrics 上报。
AdminService actual aggregation。
Web desired / actual 收敛展示。
```

## 2. 结果规则

| 结果 | 使用条件 |
| --- | --- |
| `QA_PASSED` | 对应任务验收标准和本 gate 要求均通过。 |
| `QA_PARTIAL` | 只覆盖部分 Phase 2 边界，未覆盖项、风险和后续任务已列明。 |
| `QA_BLOCKED` | 环境或依赖缺失导致最低门槛无法执行，且没有等价替代证据。 |
| `QA_FAILED` | 可复现失败、契约不一致、旧接口兜底或验收标准未达成。 |

NC-097 真实闭环集成不得在以下情况下写 `QA_PASSED`：

```text
PostgreSQL 或 Redis 未实际运行。
Agent 未启动真实 navcaster-caster。
AdminService control API 无法读到 runtime actual。
Web 只能使用 mock 展示闭环。
Caster 双 Runtime Redis Pub/Sub 未通过且没有被任务卡降级为可接受缺口。
```

## 3. Gate 分层

### Gate A：文档 / 契约冻结

适用任务：

```text
NC-090
NC-095
只改 doc/design、doc/qa 或任务卡的设计任务
```

最低检查：

```powershell
git diff --check
git status --short
```

通过标准：

```text
文档声明来源基线 feature/NC-089-v2-foundation-closed-loop-integration @ 9497a97。
文档列出 in-scope / out-of-scope。
文档列出 PG 表、Redis key、HTTP endpoint、Agent reconcile、Caster Pub/Sub 和 Web convergence。
未运行产品构建的原因写入 QA 记录或最终汇报。
git status 没有产品源码误改。
```

### Gate B：单模块开发

适用任务：

```text
NC-091 AdminService
NC-092 Agent
NC-093 Caster
NC-094 Web
```

原则：

```text
单模块任务必须运行本模块 build/test。
单模块任务不要求完整 Admin-Agent-Caster-Web 闭环。
如果依赖模块未就绪，可使用 stub/fixture，但 DEV_DONE 必须列明真实联调缺口。
```

### Gate C：关键边界 smoke

触发点：

```text
AdminService 首次连真实 PostgreSQL / Redis。
Agent 首次从 AdminService 获取 desired 并启动 Caster。
Caster 首次用真实 Redis Pub/Sub 跨 Runtime fan-out。
Web 首次使用真实 AdminService API 展示 desired/actual。
```

通过标准：

```text
边界两端都使用 v2 契约。
命令、端口、数据目录、PG DSN、Redis endpoint 和 commit 已记录。
失败时停止扩大测试范围，先修契约或环境。
```

### Gate D：Phase 2 系统闭环

触发点：

```text
NC-097 集成合入 NC-090 到 NC-095 后。
```

通过标准：

```text
Web intent -> Admin PG desired -> Redis projection -> Agent desired polling ->
Agent starts real Caster -> Caster health/metrics -> Agent events/metrics ->
Admin actual aggregation -> Web desired/actual display 至少一条链路可复现。
```

### Gate E：轻量容量基线

触发点：

```text
NC-096，且 NC-097 至少 QA_PARTIAL 以上并形成真实闭环。
```

通过标准：

```text
记录 runtime_count、worker_count、source/client 数、fan-out/pubsub counters、CPU/RSS 或等价资源指标。
不承诺 16k 正式容量，不作为 Phase 2 功能闭环硬门槛。
若发现功能性崩溃、串流或慢客户端拖垮正常 client，则转为阻断问题。
```

## 4. 模块最低命令

### 4.1 AdminService

目录：

```text
app/admin
```

最低命令：

```powershell
cd app\admin
$env:GOCACHE = "$PWD\.cache\go-build"
go test ./...
go build -o .cache\bin\navcaster-admin.exe .\cmd\navcaster-admin
```

真实 PG/Redis self-check：

```powershell
# 示例变量名由 NC-091 最终配置决定；QA 记录必须写实际使用值。
$env:NAVCASTER_ADMIN_CONFIG = ".cache\qa\admin.local.json"
.\.cache\bin\navcaster-admin.exe
curl.exe -fsS http://127.0.0.1:8080/api/v1/health
curl.exe -fsS http://127.0.0.1:8080/api/v1/control/projection-keys
```

通过标准：

```text
go test ./... PASS。
navcaster-admin 可构建。
/api/v1/health 返回 postgres=ok redis=ok，不能是 not_configured。
PostgreSQL migration 已执行或明确由 AdminService 启动执行。
创建 runtime 后 runtime_desired_states、control_intents、operation_audit_logs 可验证。
Redis v2 projection key 可验证。
v2:control:config 订阅能收到 runtime_desired_updated 或 host_desired_state_updated notify。
runtime-events / runtime-metrics ingest 后 control runtime API 可读 actual。
```

阻断项：

```text
只使用内存 repository 却宣称真实控制面通过。
Redis projection 写旧 key。
control API 无法返回 desired + actual。
runtime event 只存在内存且 NC-097 闭环后无法追溯。
```

### 4.2 Agent

目录：

```text
app/agent
```

最低命令：

```powershell
cd app\agent
$env:GOCACHE = "$PWD\.cache\go-build"
go test ./...
go build -o .cache\bin\navcaster-agent.exe .\cmd\navcaster-agent
```

真实 Caster supervisor smoke：

```powershell
# 端口、路径和配置由 NC-092 最终脚本或 QA 记录固定。
.\.cache\bin\navcaster-agent.exe -config .cache\qa\agent.local.json -once
```

通过标准：

```text
Agent 可 register 或 resume identity。
Agent 可拉取 desired-state，并记录 since_version / last_desired_version。
desired=running 时启动真实 navcaster-caster，不是 dummy runtime。
重复同一 desired generation 不重复启动进程。
Agent 可读取 Caster health/metrics。
Agent 可上报 runtime-events 和 runtime-metrics.actual[]。
desired=stopped 时可停止自己管理且 start_token 匹配的 Caster。
AdminService 暂不可用时不杀掉 last-known desired 的运行中 Caster。
```

阻断项：

```text
Agent 直接修改 PostgreSQL。
Agent 管理非本机或 start_token 不匹配进程。
AdminService 离线导致 Agent 主动停止已有 Caster。
actual snapshot 缺少 runtime_id / actual_state / observed_desired_version / updated_at。
```

### 4.3 Caster Runtime

目录：

```text
caster
```

最低命令：

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 -NtripPort 42195 -HealthPort 19195 -Mount QA_MOUNT_PHASE2
```

双 Runtime Redis Pub/Sub smoke：

```text
NC-093 必须新增或扩展脚本，参数至少包含：
  - Redis endpoint
  - Runtime A NTRIP/health port
  - Runtime B NTRIP/health port
  - mount
  - deterministic payload

最小链路：
  source -> Runtime A -> Redis v2:stream:mount:<mount> -> Runtime B -> client
```

通过标准：

```text
navcaster-caster build PASS。
self-test PASS。
单 Runtime source/client deterministic payload 完整一致。
双 Runtime Redis Pub/Sub deterministic payload 完整一致。
metrics 可区分 local fan-out、redis publish、redis subscribe/remote fan-out。
health/metrics 不依赖 AdminService。
Caster 热路径不访问 PostgreSQL。
```

阻断项：

```text
Worker 跨线程共享 session map 或 bufferevent。
跨 Runtime 依赖旧 Redis channel。
Redis Pub/Sub echo 导致本地重复 fan-out。
source/client payload 串 mount 或丢字节。
```

### 4.4 Web

目录：

```text
web
```

最低命令：

```powershell
cd app/web
npm run build
```

浏览器 / live API smoke：

```text
1. 设置 VITE_NAVCASTER_V2_MOCK=false 或等价配置。
2. 启动 Web 和 AdminService。
3. 打开 /admin/control/hosts。
4. 打开 /admin/control/runtimes。
5. 创建或选择 Runtime。
6. 执行 Start/Stop/Restart/Drain 或 worker_count desired 变更。
7. 确认页面显示 intent accepted / pending。
8. 等 Agent/Caster 上报后，确认页面显示 actual 收敛或 failed/stale。
```

通过标准：

```text
npm run build PASS。
默认 live API shape 使用 /api/v1/control/*。
Web 正确解包 response envelope。
页面并列展示 desired_state 和 actual_state。
页面展示 last heartbeat / updated_at / stale 或 offline。
Web 不直接访问 Redis、Agent local API 或 Caster internal endpoint。
浏览器 smoke 有截图、Playwright 记录或手工步骤记录。
```

阻断项：

```text
只用 mock 数据宣称闭环通过。
把 HTTP 202/accepted 显示为远端进程已完成。
沿用旧 Web API shape 作为 fallback 通过证据。
Agent offline 时仍让用户误以为操作立即执行。
```

## 5. Phase 2 系统级 Smoke 顺序

NC-097 必须按顺序执行。若某一步失败，停止后续扩大测试，记录 `QA_FAILED` 或 `QA_BLOCKED`。

### 5.1 环境准备

记录：

```text
日期
worktree
branch / commit
OS
CPU / memory
Go version
CMake / compiler / Ninja
Node / npm
PostgreSQL version / DSN masked
Redis version / endpoint
ports
```

准备：

```text
清理旧进程和端口。
清理测试 PostgreSQL schema 或使用独立 database。
清理测试 Redis v2:* key。
创建临时数据目录。
构建 AdminService、Agent、Caster、Web。
```

### 5.2 AdminService + PG + Redis

验证：

```text
run migration。
start navcaster-admin。
GET /api/v1/health -> postgres=ok redis=ok。
GET /api/v1/control/projection-keys -> 全部 v2: key。
```

### 5.3 Agent register / heartbeat

验证：

```text
start navcaster-agent。
register 或 resume identity。
heartbeat accepted。
GET /api/v1/control/hosts 显示 Host online 或 registered with heartbeat。
Redis v2:agent:heartbeat:<agent_id> 存在且 TTL 正常。
```

### 5.4 Web intent -> PG desired

验证：

```text
通过 Web 或 curl 创建 Runtime。
API 返回 intent accepted / desired_version。
PostgreSQL runtimes 和 runtime_desired_states 存在。
control_intents 和 operation_audit_logs 存在。
Web 显示 pending / applying，不能显示已完成。
```

### 5.5 Redis projection

验证：

```text
v2:config:runtime:<runtime_id> 或 Phase 2 等价 runtime projection 存在。
v2:control:desired-state:<host_id> 存在，且 shape 与 Agent desired-state response 一致。
订阅 v2:control:config 后，创建或更新 runtime desired state 可收到包含 projection key、host_id 或 runtime_id、version 的 notify。
projection payload 包含 runtime_id、config_version、listen_port、worker_count、version/checksum 或等价版本字段。
旧 ACT/MPT/STR/PULL/PUSH/CASTER key 不作为 v2 projection 证据。
```

### 5.6 Agent starts real Caster

验证：

```text
Agent 拉取 desired-state。
Agent 渲染本机 config。
Agent 启动真实 navcaster-caster 子进程。
Caster process_id 和 start_token 被 Agent 记录。
Caster local /health 和 /metrics 返回成功。
```

### 5.7 Agent actual / events / metrics

验证：

```text
Agent POST runtime-events -> 202 accepted count>0。
Agent POST runtime-metrics -> 202 accepted count>0。
AdminService GET runtime 返回 actual_state、process_id、worker_count、observed_desired_version。
PostgreSQL runtime_actual_snapshots 可验证，runtime_events 或等价持久化可验证。
Redis v2:runtime:actual:<runtime_id> 和 worker-stat 可验证或明确由 NC-091/NC-093 约定。
```

### 5.8 Web convergence

验证：

```text
Web Host/Runtime 页面显示 desired_state 与 actual_state。
actual observed_desired_version 收敛后显示 running。
停止或 restart 操作先显示 pending/applying，再显示 final actual。
Agent offline 或 stale 时页面不显示执行完成。
```

### 5.9 Single Runtime NTRIP

验证：

```text
source connects to mount。
client subscribes same mount。
source sends deterministic payload。
client receives identical payload。
metrics source_count/client_count/fanout_write_count/redis_publish_count 递增。
```

### 5.10 Dual Runtime Redis Pub/Sub

验证：

```text
Runtime A source sends deterministic payload。
Runtime B client receives identical payload via Redis v2:stream:mount:<mount>。
metrics show publish on A and subscribe/remote fan-out on B。
No duplicate echo to A local clients。
No legacy Redis channel/key used as success evidence。
```

### 5.11 Failure semantics mini-smoke

至少覆盖：

```text
Stop AdminService briefly:
  existing Caster data path remains alive。
  Agent keeps last-known desired state。

Stop Agent briefly:
  AdminService marks stale/offline after heartbeat timeout。
  AdminService does not kill Caster directly。

Kill Caster:
  Agent records event。
  restart_policy path executes。
  AdminService actual reflects failed/restarting/running。
```

如果任一 failure smoke 未运行，NC-097 不能写完整 `QA_PASSED`，除非任务卡明确降级并得到 reviewer 接受。

## 6. 轻量容量基线口径

NC-096 只建立基线，不承诺生产容量。

建议最小规模：

```text
runtime_count: 1 and 2
worker_count: 1, 2, 4
source_count: 1, 10, 100
client_count: 1, 30, 300
duration: 2 min smoke, 10 min baseline if stable
```

必报指标：

```text
connection_count
source_count
client_count
mount_count
worker_count
fanout_write_count / fanout_per_sec
redis_publish_count
redis_subscribe_count or remote_fanout_count
send_bps / recv_bps
loop_delay_ms_p95
slow_client_disconnect_count
process CPU
RSS memory
```

不达规模时：

```text
记录最后稳定规模。
记录失败点和瓶颈。
不得写成 16k 或生产容量通过。
```

## 7. 未运行项记录规则

任何 QA 记录中未运行项必须使用表格：

| 项目 | 原因 | 风险 | 后续任务 | 是否阻断当前 gate |
| --- | --- | --- | --- | --- |
| 真实 PG/Redis | 示例：本机无服务 | 无法证明 source-of-truth/projection | NC-097 | 是 |

可接受不阻断的示例：

```text
文档任务未运行产品构建。
单模块任务未运行完整系统闭环，但已运行本模块最低命令。
容量基线未在 NC-091 到 NC-095 单模块任务中运行。
无浏览器自动化时，NC-094 可先用手工截图；NC-097 仍需 live smoke 证据。
```

阻断的示例：

```text
NC-097 未运行真实 PG/Redis。
NC-097 未启动真实 navcaster-caster。
NC-097 Web 只用 mock。
NC-097 AdminService actual aggregation 无证据。
NC-097 用旧 key /旧 API /旧 Web 行为作为通过证据。
```

## 8. QA 记录模板

```markdown
# <Task ID> QA

## 结论

`QA_PASSED` / `QA_PARTIAL` / `QA_BLOCKED` / `QA_FAILED`

## 基线

- worktree:
- branch:
- commit:
- source baseline:
- date:

## 环境

- OS:
- Go:
- CMake / Ninja / compiler:
- Node / npm:
- PostgreSQL:
- Redis:
- ports:

## 验证范围

- 覆盖:
- 未覆盖:

## 命令结果

| 命令 | 结果 | 证据 / 说明 |
| --- | --- | --- |
| `git diff --check` | PASS/FAIL |  |

## 系统 smoke

| 步骤 | 结果 | 证据 |
| --- | --- | --- |
| Admin health postgres/redis | PASS/FAIL/NR |  |
| Agent register/heartbeat | PASS/FAIL/NR |  |
| Web intent -> PG desired | PASS/FAIL/NR |  |
| Redis projection + `v2:control:config` notify | PASS/FAIL/NR |  |
| Agent starts Caster | PASS/FAIL/NR |  |
| runtime-events/metrics | PASS/FAIL/NR |  |
| Web convergence | PASS/FAIL/NR |  |
| Single Runtime NTRIP | PASS/FAIL/NR |  |
| Dual Runtime Redis Pub/Sub | PASS/FAIL/NR |  |

## 未运行项

| 项目 | 原因 | 风险 | 后续任务 | 是否阻断当前 gate |
| --- | --- | --- | --- | --- |

## 阻断项

- 无 / 列表

## 备注

- 只接受 v2 契约证据；旧 API/key/protobuf/Web 不计入通过标准。
```

## 9. Reviewer 抽查清单

Reviewer 在 NC-098 至少抽查：

```text
任务卡来源基线是否仍是 NC-089 @ 9497a97 或明确合入链。
git diff --stat 是否符合岗位和任务范围。
AdminService 是否真正使用 PG source-of-truth。
Redis key 是否全部 v2: 前缀，TTL 和 projection/cache/bus 边界是否正确。
Agent 是否只管理本机 start_token 匹配进程。
Caster 热路径是否访问 PostgreSQL 或 AdminService。
Caster Worker 是否跨线程共享 session/bufferevent/map。
runtime-events 和 runtime-metrics shape 是否和文档一致。
Web 是否把 accepted 和 actual converged 分开展示。
QA 未运行项是否被合理标为阻断或非阻断。
```
