# NavCaster v2 Phase 3 Delivery QA Gate

更新时间：2026-06-27
任务：NC-100 v2 Phase 3 Production Contract And Delivery Gate
来源基线：`team-dev @ 1044bbe`
状态：Phase 3 QA / delivery gate 冻结输入，供 NC-105、NC-106、NC-107 细化和执行。
适用范围：真实 Web 浏览器 smoke、多 Runtime 联调、PG/Redis 一致性、控制指令生命周期、Runtime events/actual/metrics、Caster workers/pubsub 容量口径和交付审查。
可信度：QA 准入契约；不表示本文命令已在 NC-100 执行。

## 1. Gate 结论

Phase 3 QA 只按 v2 契约判定：

```text
不使用旧 HTTP API 作为通过证据。
不使用旧 Redis key 作为通过证据。
不使用旧 protobuf 作为通过证据。
不使用旧 Web 页面或旧命名作为通过证据。
不把 mock、memory-only、not_configured 或单次 happy path 写成生产化交付通过。
```

Phase 3 最终 delivery gate 必须覆盖：

```text
真实 PostgreSQL。
真实 Redis。
AdminService v2 HTTP/JSON。
Agent 多 Runtime desired polling and reconcile。
Agent 启动真实 navcaster-caster。
Caster health/metrics。
Caster 多 Worker 分片稳定性。
单 Runtime NTRIP source/client。
双 Runtime Redis Pub/Sub。
Agent runtime-events / runtime-metrics.actual[] 上报。
AdminService control intent lifecycle 和 actual aggregation。
Web browser live API desired/actual/events/metrics/action intent smoke。
轻量容量基线记录。
```

## 2. 结果规则

| 结果 | 使用条件 |
| --- | --- |
| `QA_PASSED` | 对应任务验收标准和本 gate 要求均通过，无阻断未运行项。 |
| `QA_PASSED_WITH_NOTED_RISKS` | 系统 gate 通过，仍有明确非阻断环境限制或后续风险。 |
| `QA_PARTIAL` | 只覆盖部分 Phase 3 边界，未覆盖项、风险和后续任务已列明。 |
| `QA_BLOCKED` | 环境或依赖缺失导致最低门槛无法执行，且没有等价替代证据。 |
| `QA_FAILED` | 可复现失败、契约不一致、旧接口兜底或验收标准未达成。 |

NC-106 不得在以下情况下写完整 `QA_PASSED`：

```text
PostgreSQL 或 Redis 未实际运行。
Web 浏览器 smoke 只用 mock。
Agent 未启动真实 navcaster-caster。
AdminService control API 无法读到 actual 和 lifecycle。
PG/Redis consistency 未检查。
Caster 双 Runtime Redis Pub/Sub 未通过。
Caster worker/pubsub 容量口径无指标记录。
旧 API/key/protobuf/Web 行为被作为通过证据。
```

## 3. Gate 分层

| Gate | 名称 | 触发任务 | 目标 |
| --- | --- | --- | --- |
| P3-0 | Contract / baseline | NC-100 | 文档冻结、基线记录、未改源码检查。 |
| P3-1 | Admin PG/Redis | NC-101 | PG source-of-truth、Redis projection/cache/bus、control lifecycle。 |
| P3-2 | Agent/Caster runtime | NC-102 | 多 Runtime reconcile、真实 Caster supervisor、actual/events/metrics。 |
| P3-3 | Caster workers/pubsub | NC-103 | worker 分片稳定性、Redis Pub/Sub 和轻量容量指标。 |
| P3-4 | Web browser production smoke | NC-104 | 真实 AdminService API、浏览器操作、desired/actual/events/metrics 展示。 |
| P3-5 | System QA matrix | NC-105 | fixture、命令、通过标准、失败分级和 NC-106 执行清单。 |
| P3-6 | Closed-loop integration | NC-106 | 合入 NC-100 到 NC-105 后执行完整系统 gate。 |
| P3-7 | Review / delivery baseline | NC-107 | 总审查、风险分级和交付基线结论。 |

## 4. P3-0 Contract / Baseline

适用任务：

```text
NC-100
只改 doc/design、doc/qa 或任务卡的设计任务
```

最低检查：

```powershell
git diff --check
git status --short
```

通过标准：

```text
文档声明来源基线 team-dev @ 1044bbe。
文档列出 Phase 3 in-scope / out-of-scope。
文档列出可持续运行、可观测、可验证、可交付四类门槛。
文档列出 AdminService/Agent/Caster/Web 第三轮集成边界。
文档列出 PG/Redis consistency、control lifecycle、runtime events/actual/metrics、worker/pubsub capacity。
文档列出 NC-101 到 NC-107 的依赖、集成顺序、QA/Reviewer 证据要求。
git status 没有 app/admin、app/agent、caster、web 产品源码误改。
未运行产品构建的原因写入 QA 记录或最终汇报。
```

## 5. P3-1 AdminService PG / Redis / Lifecycle

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
go build -o .cache\bin\navcaster-admin-selfcheck.exe .\cmd\navcaster-admin-selfcheck
```

真实 PG/Redis smoke：

```powershell
# 具体变量由 NC-101 固定，QA 记录必须写实际值。
$env:NAVCASTER_ADMIN_ADDR = "127.0.0.1:18080"
$env:NAVCASTER_ADMIN_POSTGRES_DSN = "postgres://navcaster:<redacted>@127.0.0.1:15432/navcaster_v2_phase3?sslmode=disable"
$env:NAVCASTER_ADMIN_REDIS_ADDR = "127.0.0.1:16379"
.\.cache\bin\navcaster-admin.exe
curl.exe -fsS http://127.0.0.1:18080/api/v1/health
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/projection-keys
.\.cache\bin\navcaster-admin-selfcheck.exe
```

通过标准：

```text
go test ./... PASS。
navcaster-admin 和 selfcheck 可构建。
/api/v1/health 返回 postgres=connected/ok、redis=connected/ok，不能是 not_configured。
创建 runtime/action intent 后 PG runtimes/runtime_desired_states/control_intents/operation_audit_logs 可验证。
runtime-events/runtime-metrics ingest 后 PG runtime_events/runtime_actual_snapshots 或等价 current view 可验证。
Redis v2 projection keys 可验证。
v2:control:config 订阅能收到 projection notify。
control intent lifecycle 至少能追踪 accepted -> projected -> observed/converged 或 failed/stale。
错误响应足以支持 Web 展示 pending/stale/failed。
```

阻断项：

```text
只用 memory repository 宣称真实控制面通过。
Redis projection 写旧 key。
control API 无法返回 desired + actual + lifecycle。
runtime event 只在内存中且系统 gate 后无法追溯。
payload host_id/agent_id 未绑定或校验请求级 identity。
```

## 6. P3-2 Agent / Caster Runtime Lifecycle

目录：

```text
app/agent
caster
```

最低命令：

```powershell
cd app\agent
$env:GOCACHE = "$PWD\.cache\go-build"
go test ./...
go build -o .cache\bin\navcaster-agent.exe .\cmd\navcaster-agent

cd ..\..
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
```

多 Runtime smoke：

```text
1. 启动真实 AdminService + PostgreSQL + Redis。
2. 启动 navcaster-agent。
3. 创建至少 2 个 runtime desired state，使用不同 NTRIP/health 端口。
4. Agent 拉取 desired state 并启动真实 navcaster-caster 子进程。
5. Caster /health 和 /metrics 可读。
6. Agent 上报 runtime-events 和 runtime-metrics.actual[]。
7. AdminService control API 可读到每个 runtime 的 actual_state、process_id、worker_count、observed_desired_version。
8. 执行 stop/restart/drain/no-op 或当前支持等价动作，验证 pending -> observed/converged 或 failed/stale。
```

通过标准：

```text
Agent register/resume identity 可用。
since_version / last_desired_version 增量语义正确。
重复同一 desired generation 不重复启动进程。
Agent 只停止 start_token 匹配的 Caster。
AdminService 短暂不可用时不杀掉 last-known desired 的运行中 Caster。
Caster 启动失败、health 超时、desired 覆盖和 restart_policy 路径有事件和 actual 证据。
```

阻断项：

```text
Agent 直接修改 PostgreSQL。
Agent 管理非本机或 start_token 不匹配进程。
AdminService 离线导致 Agent 主动停止已有 Caster。
actual snapshot 缺少 runtime_id / actual_state / observed_desired_version / updated_at。
异常路径被误报为 converged。
```

## 7. P3-3 Caster Workers / Redis PubSub Capacity

目录：

```text
caster
deploy/scripts
```

最低命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 -NtripPort 18091 -HealthPort 28091 -Mount QA_P3_SINGLE
.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 `
  -HostAddress 127.0.0.1 `
  -RedisHost 127.0.0.1 `
  -RedisPort 16379 `
  -RuntimeANtripPort 18091 `
  -RuntimeAHealthPort 28091 `
  -RuntimeBNtripPort 18092 `
  -RuntimeBHealthPort 28092 `
  -Mount QA_P3_PUBSUB `
  -TimeoutSeconds 20
```

容量记录最低字段：

```text
commit
OS / CPU / memory
Redis version / endpoint
runtime_count
worker_count
source_count
client_count
mount_count
payload_size
payload_rate
duration
fanout_write_count / per_sec
redis_publish_count / per_sec
redis_subscribe_message_count / per_sec
redis_remote_fanout_write_count / per_sec
redis_error_count
send_bps / recv_bps
loop_delay_ms_p95
slow_client_disconnect_count
CPU sampling command and result
RSS sampling command and result
last stable scale
failure point or none
```

通过标准：

```text
navcaster-caster build/self-test PASS。
单 Runtime deterministic payload 完整一致。
双 Runtime Redis Pub/Sub deterministic payload 完整一致。
publisher/subscriber counters 可验证。
worker_count/mount owner/local fan-out/remote fan-out metrics 可采集。
Redis Pub/Sub 使用 v2:stream:mount:<mount> 和 NCV2BUS1 envelope。
origin_runtime_id echo suppression 正常。
轻量规模内无崩溃、payload mismatch、跨 mount 串流或 Redis error 持续增长。
```

阻断项：

```text
Worker 跨线程共享 session map 或 bufferevent。
跨 Runtime 依赖旧 Redis channel。
Redis Pub/Sub echo 导致本地重复 fan-out。
source/client payload 串 mount 或丢字节。
轻量规模即出现崩溃、错发、持续 Redis error 或无法采集 counters。
```

## 8. P3-4 Web Browser Production Smoke

目录：

```text
web
```

最低命令：

```powershell
cd web
npm run build
```

浏览器 smoke：

```text
1. 使用 production build preview、dev server 或同源 QA proxy。
2. 配置 Web 连接真实 AdminService v2 API。
3. 打开 /admin/control/hosts。
4. 打开 /admin/control/runtimes。
5. 打开 runtime detail。
6. 验证 desired_state、actual_state、observed_desired_version、updated_at、stale/offline、events、metrics。
7. 发起 start/stop/restart/drain 或当前支持 action intent。
8. 页面先显示 accepted/pending/applying。
9. Agent/Caster 上报后页面显示 converged/running、failed 或 stale。
10. 记录 desktop 和 mobile 宽度的可读性检查，至少确认无明显重叠、截断或不可操作。
11. 记录 browser console error count。
```

通过标准：

```text
npm run build PASS；若失败需区分环境依赖缺失和代码问题。
浏览器 smoke 连接真实 AdminService API，不用 mock 数据替代 control API。
hosts/runtimes/detail 至少各有一次真实 API 200。
action intent 写入 AdminService，Web 不显示远端已完成，直到 actual 收敛。
failed/stale/offline/loading/empty/error 状态可见。
console error count 为 0，或非阻断已知项明确记录。
```

阻断项：

```text
只用 mock 数据宣称闭环通过。
把 HTTP 202/accepted 显示为远端执行成功。
沿用旧 Web API shape 作为 fallback 通过证据。
Web 直接访问 Redis、Agent local API 或 Caster internal endpoint。
Agent offline 时 UI 仍让用户误以为操作已执行完成。
```

## 9. P3-5 System QA Matrix

NC-105 必须把 NC-101 到 NC-104 的实际命令收束成可直接执行的矩阵。

矩阵至少包含：

```text
环境准备和清理。
Docker PostgreSQL + Redis fixture 或等价真实服务。
AdminService PG/Redis/selfcheck。
Agent multi-runtime reconcile。
Caster build/self-test/NTRIP/PubSub。
Web build/browser smoke。
PG/Redis consistency SQL/redis-cli commands。
control intent lifecycle checks。
runtime-events/actual/metrics checks。
worker/pubsub capacity checks。
failure semantics mini-smoke。
未运行项模板。
缺陷分级。
证据路径约定。
```

NC-105 不替代 NC-106 最终系统执行，但必须提供 NC-106 可直接复用的命令和参数。

## 10. P3-6 Closed-loop Integration

NC-106 必须等待 NC-100 到 NC-105 均 DEV_DONE 后开始。

### 10.1 环境准备

记录：

```text
date
worktree
branch / commit
merged task commits
OS
CPU / memory
Go version
CMake / Ninja / compiler
Node / npm
PostgreSQL version / DSN masked
Redis version / endpoint
ports
browser / automation tool
```

准备：

```text
清理旧进程和端口。
清理测试 PostgreSQL schema 或使用独立 database。
清理测试 Redis v2:* key。
创建临时数据目录。
构建 AdminService、Agent、Caster、Web。
优先使用已知可用低端口规避当前 Windows 422xx bind 风险。
```

### 10.2 系统 smoke 顺序

NC-106 必须按顺序执行。若某一步失败，停止扩大测试，记录 `QA_FAILED` 或 `QA_BLOCKED`。

```text
1. git diff --check and status boundary。
2. AdminService + PG + Redis health/selfcheck。
3. Agent register / heartbeat。
4. Web intent 或 Admin API 创建 Runtime desired。
5. PG desired/control_intents/audit SQL check。
6. Redis projection + v2:control:config notify check。
7. Agent starts real Caster for at least 2 runtimes。
8. Caster health/metrics check。
9. Agent runtime-events/runtime-metrics.actual[] ingest。
10. AdminService control API desired + actual + lifecycle readback。
11. Web convergence browser smoke。
12. Single Runtime NTRIP payload smoke。
13. Dual Runtime Redis Pub/Sub payload smoke。
14. Worker partition / mount owner / counter check。
15. Light capacity baseline record。
16. Failure semantics mini-smoke。
17. Cleanup and residual process/key check。
```

### 10.3 Failure semantics mini-smoke

至少覆盖：

```text
Stop AdminService briefly:
  existing Caster data path remains alive。
  Agent keeps last-known desired state。
  after recovery, Agent reports actual/events。

Stop Agent briefly:
  AdminService marks stale/offline after heartbeat timeout。
  AdminService does not kill Caster directly。
  Web does not show pending intent as completed。

Kill Caster:
  Agent records process_exited / restart_scheduled or failed event。
  restart_policy path executes when configured。
  AdminService actual reflects failed/restarting/running。
```

若任一 failure smoke 未运行，NC-106 不能写完整 `QA_PASSED`，除非任务卡明确降级并得到 Reviewer 接受。

## 11. P3-7 Review / Delivery Baseline

NC-107 Reviewer 必须以代码审查姿态检查：

```text
NC-100 到 NC-106 diff 是否符合岗位边界。
QA 记录是否覆盖本 gate 的关键证据。
未运行项是否有原因、风险、后续任务和阻断判定。
AdminService 是否真正使用 PG source-of-truth。
Redis key/channel 是否全部按 v2 契约作为通过证据。
Agent 是否只管理本机 start_token 匹配进程。
Caster 热路径是否访问 PostgreSQL 或 AdminService。
Caster Worker 是否跨线程共享 session/bufferevent/map。
runtime-events、actual snapshots、metrics shape 是否和文档一致。
Web 是否把 accepted 和 actual converged 分开展示。
容量基线是否被误写成生产容量承诺。
```

Reviewer 结论：

```text
APPROVED
APPROVED_WITH_NOTED_RISKS
CHANGES_REQUESTED
BLOCKED
```

批准只能表示：

```text
可作为 Phase 3 delivery baseline 继续演进。
不表示生产发布批准。
不表示 16k 正式容量通过。
```

## 12. 未运行项记录规则

所有 QA 记录中未运行项必须使用表格：

| 项目 | 原因 | 风险 | 后续任务 | 是否阻断当前 gate |
| --- | --- | --- | --- | --- |
| 真实 PG/Redis | 示例：本机无 Docker/服务 | 无法证明 source-of-truth/projection | NC-106 | 是 |

可接受不阻断的示例：

```text
NC-100 文档任务未运行产品构建。
单模块任务未运行完整系统闭环，但已运行本模块最低命令。
NC-103 不承诺 16k，只记录轻量容量基线。
NC-104 无 Playwright 时可用手工浏览器记录，但 NC-106 仍需 live browser smoke 证据。
```

阻断示例：

```text
NC-106 未运行真实 PG/Redis。
NC-106 未启动真实 navcaster-caster。
NC-106 Web 只用 mock。
NC-106 AdminService actual/lifecycle aggregation 无证据。
NC-106 Caster Redis Pub/Sub 无真实 Redis 证据。
NC-106 用旧 key /旧 API /旧 Web 行为作为通过证据。
```

## 13. QA 记录模板

```markdown
# <Task ID> QA

## 结论

`QA_PASSED` / `QA_PASSED_WITH_NOTED_RISKS` / `QA_PARTIAL` / `QA_BLOCKED` / `QA_FAILED`

## 基线

- worktree:
- branch:
- commit:
- source baseline:
- date:
- merged task commits:

## 环境

- OS:
- CPU / memory:
- Go:
- CMake / Ninja / compiler:
- Node / npm:
- PostgreSQL:
- Redis:
- ports:
- browser:

## 验证范围

- 覆盖:
- 未覆盖:
- 明确排除的旧系统证据:

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
| runtime-events/metrics actual | PASS/FAIL/NR |  |
| Admin actual/lifecycle readback | PASS/FAIL/NR |  |
| Web convergence browser smoke | PASS/FAIL/NR |  |
| Single Runtime NTRIP | PASS/FAIL/NR |  |
| Dual Runtime Redis Pub/Sub | PASS/FAIL/NR |  |
| Worker partition / mount owner | PASS/FAIL/NR |  |
| Light capacity baseline | PASS/FAIL/NR |  |
| Failure semantics mini-smoke | PASS/FAIL/NR |  |

## PG / Redis 一致性

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| runtime_desired_states version | PASS/FAIL/NR |  |
| control_intents lifecycle | PASS/FAIL/NR |  |
| runtime_events persisted | PASS/FAIL/NR |  |
| runtime_actual_snapshots/current actual | PASS/FAIL/NR |  |
| Redis projection keys | PASS/FAIL/NR |  |
| Redis notify | PASS/FAIL/NR |  |
| Runtime TTL keys | PASS/FAIL/NR |  |

## 容量记录

| 指标 | 值 |
| --- | --- |
| runtime_count |  |
| worker_count |  |
| source_count |  |
| client_count |  |
| duration |  |
| fanout/pubsub counters |  |
| CPU/RSS sampling |  |
| last stable scale |  |
| failure point |  |

## 未运行项

| 项目 | 原因 | 风险 | 后续任务 | 是否阻断当前 gate |
| --- | --- | --- | --- | --- |

## 阻断项

- 无 / 列表

## 备注

- 只接受 v2 契约证据；旧 API/key/protobuf/Web 不计入通过标准。
```
