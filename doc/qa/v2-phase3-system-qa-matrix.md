# NavCaster v2 Phase 3 System QA Matrix

更新时间：2026-06-27
任务：NC-105 v2 Phase 3 System QA Matrix
来源基线：`team-dev @ 1044bbe`
状态：Phase 3 系统 QA 执行矩阵，供 NC-106 集成 QA 和 NC-107 Reviewer 使用。
适用范围：真实 PostgreSQL、Redis、AdminService、Agent、navcaster-caster、Web 浏览器 smoke、多 Runtime、Runtime events/actual/metrics、Caster workers/pubsub capacity。

## 1. QA 结论口径

Phase 3 只接受 v2 证据：

```text
不兼容旧架构。
不兼容旧 HTTP API。
不兼容旧 Redis key。
不兼容旧 protobuf。
不兼容旧命名规则。
不兼容旧 Web 风格。
不使用 CasterService 旧闭环作为 v2 通过证据。
```

NC-105 本身是 QA 矩阵和轻量基线复核任务，不执行最终系统闭环。最终闭环在 NC-106 执行，NC-107 只基于证据审查。

Phase 3 系统闭环必须覆盖：

```text
Web browser action intent
  -> AdminService HTTP/JSON control API
  -> PostgreSQL desired/control_intents/audit
  -> Redis v2 projection and notification
  -> Agent desired polling/reconcile
  -> Agent starts multiple real navcaster-caster runtimes
  -> Caster health/metrics/NTRIP/workers/Redis Pub/Sub
  -> Agent runtime-events/runtime-metrics actual upload
  -> AdminService desired/actual/events aggregation
  -> Web desired/actual convergence and stale/failed display
```

## 2. Result Rules

| 结果 | 使用条件 |
| --- | --- |
| `QA_PASSED` | 当前任务验收标准和对应 gate 必跑项均通过，没有阻断缺口。 |
| `QA_PASSED_WITH_NOTED_RISKS` | 系统闭环通过，但存在明确非阻断环境/自动化/容量风险，Reviewer 可接受。 |
| `QA_PARTIAL` | 只覆盖部分 gate，未覆盖项、风险、后续任务和是否阻断已列明。 |
| `QA_BLOCKED` | 环境或依赖缺失导致 gate 最低门槛无法运行，且没有等价替代证据。 |
| `QA_FAILED` | 可复现失败、契约不一致、旧系统兜底、数据不一致或验收标准未达成。 |

NC-106 不得在以下情况下写完整 `QA_PASSED`：

```text
真实 PostgreSQL 或 Redis 未运行。
AdminService 使用 memory repository 或 health 显示 postgres/redis not_configured。
Agent 未启动真实 navcaster-caster，或只启动 dummy runtime。
未完成至少 2 个 runtime 的 Agent/Caster 联调。
Web 浏览器 smoke 只使用 mock 数据。
PG/Redis desired/projection/control_intents/events/actual 不能交叉校验。
控制 action intent 只有 accepted，没有 observed/converged/failed/stale 证据。
Runtime events 和 actual snapshot 不能追踪真实进程状态。
Caster Redis Pub/Sub 使用旧 channel 或无 payload/counter 证据。
```

## 3. Environment And Fixture Rules

### 3.1 Required Local Ports

默认端口：

| 服务 | 地址 |
| --- | --- |
| PostgreSQL | `127.0.0.1:15432` |
| Redis | `127.0.0.1:16379` |
| AdminService | `127.0.0.1:18080` |
| Web dev/preview | `127.0.0.1:5173` / `127.0.0.1:4173` |
| Runtime A | NTRIP `42195`, health `19195` |
| Runtime B | NTRIP `42196`, health `19196` |
| Runtime C optional | NTRIP `42197`, health `19197` |

端口冲突时可以上调端口，但 QA 记录必须写实际端口。

### 3.2 Docker PostgreSQL + Redis Fixture Prepare

PowerShell 准备命令：

```powershell
$env:NAVCASTER_PG_CONTAINER = "navcaster-p3-qa-pg"
$env:NAVCASTER_REDIS_CONTAINER = "navcaster-p3-qa-redis"
$env:NAVCASTER_ADMIN_POSTGRES_DSN = "postgres://navcaster:navcaster@127.0.0.1:15432/navcaster_p3_qa?sslmode=disable"
$env:NAVCASTER_ADMIN_REDIS_ADDR = "127.0.0.1:16379"
$env:NAVCASTER_ADMIN_ADDR = "127.0.0.1:18080"
$env:NAVCASTER_ADMIN_BOOTSTRAP_TOKEN = "navcaster-p3-qa-bootstrap"

docker run --rm -d --name $env:NAVCASTER_PG_CONTAINER `
  -e POSTGRES_USER=navcaster `
  -e POSTGRES_PASSWORD=navcaster `
  -e POSTGRES_DB=navcaster_p3_qa `
  -p 15432:5432 postgres:16

docker run --rm -d --name $env:NAVCASTER_REDIS_CONTAINER `
  -p 16379:6379 redis:8.6.3

docker exec $env:NAVCASTER_PG_CONTAINER pg_isready -U navcaster -d navcaster_p3_qa
docker exec $env:NAVCASTER_REDIS_CONTAINER redis-cli ping
```

Fixture 隔离规则：

```text
PostgreSQL 只使用 navcaster_p3_qa 或唯一测试库。
Redis 只检查和清理 v2:* 测试 key，不用旧 ACT/MPT/STR/PULL/PUSH/CASTER key 作为通过证据。
测试进程必须使用唯一 state/work/runtime 目录，例如 build/p3-qa/<gate>。
所有 token、DSN、账号密码在 QA 记录中脱敏。
同一台机器不要并行运行多个占用 15432/16379/42195/42196/19195/19196 的 smoke。
```

### 3.3 Fixture Cleanup

每个 gate 成功或失败后都必须清理：

```powershell
# Stop local processes by captured PID first. Do not use broad process kill unless PID evidence is recorded.

docker exec $env:NAVCASTER_REDIS_CONTAINER redis-cli --scan --pattern "v2:*"
docker exec $env:NAVCASTER_REDIS_CONTAINER redis-cli --scan --pattern "v2:*" | ForEach-Object {
  docker exec $env:NAVCASTER_REDIS_CONTAINER redis-cli DEL $_ | Out-Null
}

docker exec $env:NAVCASTER_PG_CONTAINER psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "TRUNCATE runtime_events, runtime_actual_snapshots, control_intents, operation_audit_logs, runtime_desired_states, runtimes, agents, hosts RESTART IDENTITY CASCADE;"

docker rm -f $env:NAVCASTER_REDIS_CONTAINER
docker rm -f $env:NAVCASTER_PG_CONTAINER

Get-ChildItem -LiteralPath .\build\p3-qa -ErrorAction SilentlyContinue
```

如果 cleanup 失败，QA 记录必须列出残留 container、process、port、Redis key、PG row 和临时目录。

## 4. Baseline Readiness

启动基线已知事实：

```text
日期：2026-06-27
基线：team-dev @ 1044bbe
PASS CMake Ninja configure。
PASS schema_smoke Ninja build。
PASS schema_smoke executable run。
PASS navcaster-caster build。
ENV_GAP Web production build failed because web\node_modules\.bin\tsc.cmd is missing.
```

NC-105 最低复核只做轻量命令，不重复最终系统 smoke：

```powershell
git status --short --branch
git diff --check
Test-Path .\bin\Release\schema_smoke.exe
Test-Path .\bin\Release\navcaster-caster.exe
.\bin\Release\schema_smoke.exe
Test-Path .\web\node_modules\.bin\tsc.cmd
```

如果 `schema_smoke.exe` 或 `navcaster-caster.exe` 不存在，可按需要运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target schema_smoke
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
```

NC-105 收口复核记录：

```text
PASS git status --short --branch:
  feature/NC-105-v2-phase3-system-qa-matrix only has doc/qa changes.

PASS git rev-parse HEAD:
  1044bbeeb35f2743bf76f9b7ffe5700a0db15ac4.

PASS rg -n "P3-[0-8]" doc/qa/v2-phase3-system-qa-matrix.md:
  P3-0 到 P3-8 gate 均有矩阵行和分节口径。

PASS git diff --check:
  no whitespace error; Git reports LF-to-CRLF normalization warning for doc/qa/README.md only.

PASS Test-Path .\bin\Release\schema_smoke.exe:
  existing Release artifact found.

PASS Test-Path .\bin\Release\navcaster-caster.exe:
  existing Release artifact found.

PASS .\bin\Release\schema_smoke.exe:
  [schema_smoke] all checks passed.

ENV_GAP Test-Path .\web\node_modules\.bin\tsc.cmd:
  False; Web build/browser smoke must be rerun by NC-104/NC-106 after dependency repair.

PASS no product source diff:
  git diff --name-only -- admin agent caster web src deploy proto tools returns empty.
```

Baseline consistency note:

```text
This NC-105 worktree is based on team-dev @ 1044bbe and does not yet contain
the NC-100 delivery gate files because NC-106 is responsible for merging
NC-100 through NC-105 in order. NC-105 consistency was cross-checked against
NC-100 task/QA records and the NC-101 to NC-107 task cards. NC-106 must rerun
the final gate after those branches are integrated into one commit.
```

## 5. Phase 3 Gate Matrix

| Gate | 目标 | 必跑阶段 | 主要证据 | 结果限制 |
| --- | --- | --- | --- | --- |
| P3-0 baseline readiness | 确认基线、工具、构建产物和文档边界。 | NC-105 / NC-106 | git status/diff、schema_smoke、navcaster-caster artifact、Web build gap。 | Web build 环境缺口必须记录，不能掩盖代码失败。 |
| P3-1 Admin PG/Redis gate | AdminService 使用 PG source-of-truth 和 Redis v2 projection。 | NC-101 / NC-106 | API、SQL、redis-cli、projection notify。 | memory/not_configured 不能通过。 |
| P3-2 Agent/Caster multi-runtime gate | Agent 管理至少 2 个真实 Caster runtime。 | NC-102 / NC-106 | Agent logs、process_id/start_token、health/metrics、Admin actual。 | dummy runtime 不能通过。 |
| P3-3 Web browser smoke | 浏览器连接真实 AdminService，展示和提交 action intent。 | NC-104 / NC-106 | build、browser screenshot/trace/console、API network。 | mock-only 不能通过。 |
| P3-4 control intent lifecycle | accepted -> projected -> observed -> converged/failed/stale 可审计。 | NC-101 / NC-106 | control_intents、operation_audit_logs、runtime_desired_states、events。 | accepted 不能等同执行完成。 |
| P3-5 Runtime events/actual tracking | Runtime events、actual snapshots、metrics 追踪真实进程状态。 | NC-101 / NC-102 / NC-106 | runtime_events、runtime_actual_snapshots、runtime-metrics payload、Web events。 | older actual 覆盖 newer actual 为失败。 |
| P3-6 Caster workers/pubsub capacity | 多 Worker 分片、Redis Pub/Sub、轻量容量指标。 | NC-103 / NC-106 | NTRIP payload、metrics counters、CPU/RSS、Redis channel。 | 不承诺 16k，不跑容量需标未运行。 |
| P3-7 integration smoke | 串起 Web/Admin/PG/Redis/Agent/Caster/Web 闭环。 | NC-106 | 集成脚本、SQL/Redis/browser/metrics 证据。 | 任一核心边界未跑不能写完整 PASS。 |
| P3-8 reviewer evidence input | 给 NC-107 提供可审查证据包和风险分级。 | NC-105 / NC-106 | QA 记录、命令表、未运行项、缺陷列表。 | 不能用“未发现问题”替代证据覆盖。 |

## 6. P3-0 Baseline Readiness

前置环境：

```text
worktree 位于待测任务分支。
来源基线和合入顺序已记录。
未合入的并行任务不能作为当前 commit 的通过证据。
```

命令：

```powershell
git status --short --branch
git rev-parse HEAD
git diff --name-status team-dev...HEAD
git diff --check
go version
node --version
npm --version
cmake --version
docker version
Test-Path .\bin\Release\schema_smoke.exe
Test-Path .\bin\Release\navcaster-caster.exe
Test-Path .\web\node_modules\.bin\tsc.cmd
```

通过标准：

```text
基线和当前 commit 明确。
diff 范围符合岗位和任务边界。
没有产品源码误改，除非任务明确要求。
schema_smoke 和 navcaster-caster 已通过或可按命令复跑。
Web build 环境缺口记录为 tsc.cmd missing，NC-104/NC-106 必须修复或重装依赖后复跑。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| diff 中包含未说明产品行为变更 | S1 |
| `git diff --check` 失败 | S2 |
| schema_smoke 或 navcaster-caster 基线复核失败 | S1 |
| Web build 仅因 `tsc.cmd` 缺失失败 | S3 环境缺口，NC-104/NC-106 必须闭合 |

## 7. P3-1 Admin PG/Redis Gate

前置环境：

```text
PostgreSQL 16 fixture 可访问。
Redis 8.6.3 fixture 可访问。
AdminService 从当前 commit 构建。
NAVCASTER_ADMIN_POSTGRES_DSN 和 NAVCASTER_ADMIN_REDIS_ADDR 均设置。
```

构建和启动：

```powershell
cd app\admin
$env:GOCACHE = "$PWD\.cache\go-build"
go test ./...
go build -o .cache\bin\navcaster-admin.exe .\cmd\navcaster-admin
$env:NAVCASTER_ADMIN_ADDR = "127.0.0.1:18080"
$env:NAVCASTER_ADMIN_POSTGRES_DSN = "postgres://navcaster:navcaster@127.0.0.1:15432/navcaster_p3_qa?sslmode=disable"
$env:NAVCASTER_ADMIN_REDIS_ADDR = "127.0.0.1:16379"
$env:NAVCASTER_ADMIN_BOOTSTRAP_TOKEN = "navcaster-p3-qa-bootstrap"
.\.cache\bin\navcaster-admin.exe *> ..\..\build\p3-qa\admin.log
```

API smoke：

```powershell
curl.exe -fsS http://127.0.0.1:18080/api/v1/health
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/projection-keys
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/hosts
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/register `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer navcaster-p3-qa-bootstrap" `
  --data "@build/p3-qa/agent-register.json"
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/control/runtimes `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer navcaster-p3-qa-bootstrap" `
  --data "@build/p3-qa/runtime-create-a.json"
```

PostgreSQL consistency SQL：

```powershell
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, host_id, status, updated_at FROM runtimes ORDER BY updated_at DESC LIMIT 5;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, desired_state, version, generation, listen_port, worker_count, updated_at FROM runtime_desired_states ORDER BY updated_at DESC LIMIT 5;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT intent_id, kind, status, desired_version, runtime_id, created_at FROM control_intents ORDER BY created_at DESC LIMIT 10;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT action, target_type, target_id, created_at FROM operation_audit_logs ORDER BY created_at DESC LIMIT 10;"
```

Redis consistency checks：

```powershell
docker exec navcaster-p3-qa-redis redis-cli --scan --pattern "v2:*"
docker exec navcaster-p3-qa-redis redis-cli GET "v2:config:runtime:<runtime_id>"
docker exec navcaster-p3-qa-redis redis-cli GET "v2:control:desired-state:<host_id>"
docker exec navcaster-p3-qa-redis redis-cli --raw SUBSCRIBE "v2:control:config"
```

通过标准：

```text
/api/v1/health 中 postgres=ok 且 redis=ok。
/api/v1/control/projection-keys 只列出 v2:* key/channel。
创建 runtime 后 PG runtimes、runtime_desired_states、control_intents、operation_audit_logs 可交叉验证。
Redis v2:config:runtime:<runtime_id> 和 v2:control:desired-state:<host_id> 与 PG desired version/generation/config_version 一致。
v2:control:config notify 包含 kind、runtime_id 或 host_id、projection、version。
删除 Redis projection 后可由 PG rebuild 或明确记录为 NC-101/NC-106 缺口。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| health postgres/redis 不是 ok | S1 |
| runtime desired 写 PG 成功但 Redis projection 永久缺失 | S1 |
| Redis projection 与 PG desired version/generation 不一致 | S1 |
| control_intents/audit 缺失 | S2 |
| projection key 使用旧前缀 | S0 |

## 8. P3-2 Agent/Caster Multi-Runtime Gate

前置环境：

```text
P3-1 AdminService 真实 PG/Redis 已通过或正在运行。
navcaster-caster Release artifact 存在。
Agent work/state/runtime 目录为空且唯一。
Runtime A/B 端口不冲突。
```

构建：

```powershell
cd app\agent
$env:GOCACHE = "$PWD\.cache\go-build"
go test ./...
go build -o .cache\bin\navcaster-agent.exe .\cmd\navcaster-agent
cd ..\..
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
```

当前 Agent 配置示例：

```json
{
  "admin_url": "http://127.0.0.1:18080",
  "bootstrap_token": "navcaster-p3-qa-bootstrap",
  "agent_id": "",
  "agent_secret": "",
  "host_id": "",
  "state_path": "build/p3-qa/agent/agent_state.json",
  "runtime_root": "build/p3-qa/agent/runtime",
  "heartbeat_interval": "5s",
  "poll_interval": "2s",
  "request_timeout": "5s",
  "stop_timeout": "5s"
}
```

运行：

```powershell
.\app\agent\.cache\bin\navcaster-agent.exe -config .\build\p3-qa\agent.json *> .\build\p3-qa\agent.log
```

NC-106 预期多 Runtime 编排脚本：

```powershell
.\deploy\scripts\v2_phase3_multi_runtime_smoke.ps1 `
  -AdminBaseUrl http://127.0.0.1:18080 `
  -CasterExe .\bin\Release\navcaster-caster.exe `
  -StateDir .\build\p3-qa\agent `
  -RuntimeANtripPort 42195 `
  -RuntimeAHealthPort 19195 `
  -RuntimeBNtripPort 42196 `
  -RuntimeBHealthPort 19196
```

验证命令：

```powershell
curl.exe -fsS http://127.0.0.1:19195/health
curl.exe -fsS http://127.0.0.1:19195/metrics
curl.exe -fsS http://127.0.0.1:19196/health
curl.exe -fsS http://127.0.0.1:19196/metrics
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/hosts
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/runtimes
```

PG/Redis actual checks：

```powershell
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, actual_state, process_id, start_token, listen_port, worker_count, observed_desired_version, redis_connected, observed_at FROM runtime_actual_snapshots ORDER BY observed_at DESC LIMIT 20;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, type, severity, desired_version, process_id, occurred_at FROM runtime_events ORDER BY occurred_at DESC LIMIT 30;"
docker exec navcaster-p3-qa-redis redis-cli GET "v2:runtime:actual:<runtime_id_a>"
docker exec navcaster-p3-qa-redis redis-cli GET "v2:runtime:actual:<runtime_id_b>"
```

通过标准：

```text
Agent register/resume 成功，heartbeat 持续上报。
至少两个 desired runtime 被 Agent 拉取并启动真实 navcaster-caster。
两个 runtime 的 process_id、start_token、listen_port、health_port、worker_count 可区分。
重复 reconcile 同一 desired version/generation 不重复启动进程。
desired=stopped 只停止 start_token 匹配且由本 Agent 管理的 runtime。
AdminService 短暂不可用时，Agent 不主动杀掉 last-known desired=running 的 Caster。
Agent 恢复后补传 actual/events/metrics。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| 只能启动 dummy-runtime | S1 |
| 同一 desired 重复启动多个进程 | S1 |
| 停止了 start_token 不匹配或非本机进程 | S0 |
| AdminService 短断导致 Caster 被 Agent 停止 | S1 |
| actual 缺少 process_id/start_token/observed_desired_version | S2 |

## 9. P3-3 Web Browser Smoke

前置环境：

```text
Web dependencies 完整，web\node_modules\.bin\tsc.cmd 存在。
AdminService 真实 PG/Redis 正在运行。
已有至少一个 host、两个 runtime、actual/events/metrics 测试数据。
浏览器自动化优先 Playwright；不可用时保留手工截图和 console/network 记录。
```

构建：

```powershell
cd web
npm run build
npm run preview -- --host 127.0.0.1 --port 4173
```

开发模式：

```powershell
cd web
npm run dev -- --host 127.0.0.1 --port 5173
```

真实浏览器步骤：

```text
1. 打开 http://127.0.0.1:5173/admin/control/hosts。
2. 登录或使用测试认证上下文。
3. Hosts list 显示 agent online/stale/offline、last heartbeat、resource summary。
4. 打开 /admin/control/runtimes，显示 desired_state、actual_state、config_version、observed_desired_version、worker_count。
5. 打开 runtime detail，显示 process_id、redis_connected、events、actual metrics、worker counters。
6. 发起 start/stop/restart/drain 或当前 AdminService 支持的 action intent。
7. 页面先显示 intent accepted/pending/applying。
8. Agent/Caster 上报后页面显示 converged running/stopped 或 failed/stale。
9. 刷新页面后状态仍来自 AdminService live API。
10. 桌面宽度和移动宽度无明显重叠、截断或不可操作状态。
```

建议 Playwright 口径：

```powershell
cd web
npx playwright test .\tests\v2-phase3-control.spec.ts --project=chromium --trace on
```

如果项目尚未引入 Playwright，NC-104/NC-106 可先用手工证据，但必须记录：

```text
截图路径。
浏览器 console error count。
Network 中 /api/v1/control/* 和 /api/v1/events/stream 请求。
action intent request/response。
刷新后 live API response。
桌面和移动 viewport。
```

通过标准：

```text
npm run build PASS。
浏览器使用真实 AdminService v2 API，不使用 mock detail 作为通过证据。
Web 正确区分 accepted/pending/applying 与 actual converged。
failed/stale/offline 状态可见。
console error count 为 0，或仅有明确非阻断已知项。
UI 在 1366x768 和 390x844 无关键文本重叠、按钮不可点或表格遮挡。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| Web 只连 mock API 却宣称 live smoke | S1 |
| 202/accepted 被显示为进程已完成 | S1 |
| action intent 无法提交或无错误反馈 | S1 |
| npm build 因代码 TypeScript 错误失败 | S1 |
| npm build 因本机 `tsc.cmd` 缺失失败 | S3 环境缺口 |
| 移动/桌面关键状态不可读或按钮不可操作 | S2 |

## 10. P3-4 Control Intent Lifecycle

目标状态机：

```text
accepted
  -> projected
  -> observed
  -> converged
```

失败/替代终态：

```text
superseded
failed
stale
timeout
```

如果代码内部状态名不同，QA 可以映射，但必须在记录中列出映射表。

API 命令：

```powershell
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/control/runtimes/<runtime_id>/actions/restart `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer navcaster-p3-qa-bootstrap" `
  --data "{`"request_id`":`"p3-restart-001`",`"reason`":`"phase3 smoke`"}"

curl.exe -fsS http://127.0.0.1:18080/api/v1/control/runtimes/<runtime_id>
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/events?runtime_id=<runtime_id>
```

SQL timeline：

```powershell
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT intent_id, request_id, kind, status, desired_version, runtime_id, created_at FROM control_intents WHERE runtime_id='<runtime_id>' ORDER BY created_at;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, desired_state, action_intent, version, generation, updated_at FROM runtime_desired_states WHERE runtime_id='<runtime_id>';"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT type, severity, desired_version, process_id, occurred_at, message FROM runtime_events WHERE runtime_id='<runtime_id>' ORDER BY occurred_at;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT actual_state, process_id, observed_desired_version, observed_at, last_error FROM runtime_actual_snapshots WHERE runtime_id='<runtime_id>' ORDER BY observed_at DESC LIMIT 20;"
```

通过标准：

```text
POST action 返回 intent_id、status=accepted 或等价 accepted、desired_version。
control_intents 写入 request_id/kind/status/desired_version/runtime_id。
operation_audit_logs 写入 actor/action/target。
runtime_desired_states version/generation 单调递增。
Redis projection 与 desired_version 或 generation 对齐。
Agent event 能证明 desired received/applied、process stop/start/restart 或 failure。
actual observed_desired_version >= desired_version 时才算 converged。
Web 在 observed 前不显示执行完成。
重复 request_id + 相同 body 幂等；重复 request_id + 不同 body 按契约拒绝或记录冲突。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| accepted 后没有 PG 审计事实 | S1 |
| Redis projection 版本回退 | S1 |
| actual 未观察 desired 却标 completed/converged | S1 |
| stale/offline 被显示为成功 | S1 |
| 幂等 request 产生重复冲突动作 | S2 |

## 11. P3-5 Runtime Events / Actual Tracking

前置环境：

```text
P3-2 至少有一个 running runtime。
Agent 能 POST runtime-events 和 runtime-metrics。
AdminService control runtime API 能返回 desired + actual。
```

事件注入/观察：

```powershell
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/<agent_id>/runtime-events `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer navcaster-p3-qa-bootstrap" `
  --data "@build/p3-qa/runtime-events.json"

curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/<agent_id>/runtime-metrics `
  -H "Content-Type: application/json" `
  -H "Authorization: Bearer navcaster-p3-qa-bootstrap" `
  --data "@build/p3-qa/runtime-metrics.json"
```

SQL checks：

```powershell
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT event_id, runtime_id, type, severity, desired_version, process_id, occurred_at, ingested_at FROM runtime_events ORDER BY ingested_at DESC LIMIT 30;"
docker exec navcaster-p3-qa-pg psql -U navcaster -d navcaster_p3_qa -v ON_ERROR_STOP=1 -c "SELECT runtime_id, actual_state, process_id, start_token, config_version, listen_port, worker_count, connections, sources, clients, loop_delay_ms_p95, redis_connected, observed_desired_version, observed_at FROM runtime_actual_snapshots ORDER BY observed_at DESC LIMIT 30;"
```

Redis checks:

```powershell
docker exec navcaster-p3-qa-redis redis-cli GET "v2:runtime:actual:<runtime_id>"
docker exec navcaster-p3-qa-redis redis-cli HGETALL "v2:runtime:worker-stat:<runtime_id>"
docker exec navcaster-p3-qa-redis redis-cli HGETALL "v2:runtime:mount-owner:<runtime_id>"
docker exec navcaster-p3-qa-redis redis-cli TTL "v2:runtime:actual:<runtime_id>"
```

通过标准：

```text
runtime_events 以 event_id 幂等，同一 agent_id/event_id 不重复产生多条事实。
runtime_actual_snapshots 保存真实 process_id/start_token/listen_port/worker_count。
AdminService current actual 不被 older observed_at snapshot 覆盖。
Redis v2:runtime:actual:<runtime_id> TTL 为正。
worker-stat 和 mount-owner 的 runtime_id 与 Admin actual 一致。
Web runtime detail 能展示事件时间线、last_error、redis_connected、worker/source/client counters。
Kill Caster 后能看到 process_exited 或 runtime_health_failed，restart_policy 生效后有 restarted/running 证据。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| fake/stale actual 覆盖 newer running actual | S1 |
| runtime event 缺少 runtime_id 或无法关联 actual | S1 |
| Caster 崩溃后没有事件或 actual 仍显示健康 | S1 |
| Redis actual TTL 不存在或永久残留 | S2 |
| Web 不显示 failed/stale/last_error | S2 |

## 12. P3-6 Caster Workers / PubSub Capacity

前置环境：

```text
navcaster-caster Release artifact 存在。
Redis 8.6.3 fixture 可访问。
NTRIP/health 端口无冲突。
不与 P3-7 完整闭环并行争用相同端口。
```

现有可执行命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250

.\deploy\scripts\v2_caster_ntrip_smoke.ps1 `
  -NtripPort 42195 `
  -HealthPort 19195 `
  -Mount QA_MOUNT_P3_A `
  -WorkerCount 2

.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 `
  -RedisHost 127.0.0.1 `
  -RedisPort 16379 `
  -RuntimeANtripPort 42195 `
  -RuntimeAHealthPort 19195 `
  -RuntimeBNtripPort 42196 `
  -RuntimeBHealthPort 19196 `
  -Mount QA_MOUNT_P3_PUBSUB `
  -WorkerCount 2
```

Metrics checks：

```powershell
curl.exe -fsS http://127.0.0.1:19195/metrics
curl.exe -fsS http://127.0.0.1:19196/metrics
Get-Process navcaster-caster | Select-Object Id,CPU,WorkingSet64,PrivateMemorySize64,StartTime
docker exec navcaster-p3-qa-redis redis-cli PUBSUB CHANNELS "v2:stream:mount:*"
```

轻量容量建议命令：

```powershell
.\deploy\scripts\v2_caster_capacity_baseline.ps1 `
  -RedisHost 127.0.0.1 `
  -RedisPort 16379 `
  -RuntimeCount 2 `
  -WorkerCount 1,2,4 `
  -SourceCount 2 `
  -ClientsPerSource 3 `
  -PayloadBytes 256 `
  -PayloadHz 5 `
  -DurationSec 120 `
  -SampleIntervalSec 10 `
  -OutDir .\build\p3-qa\capacity
```

如果该脚本尚未存在，NC-103 必须提供脚本或等价手工命令，并记录同等字段。

必报字段：

| 字段 | 说明 |
| --- | --- |
| commit | 待测 commit。 |
| runtime_count | 至少 1 和 2。 |
| worker_count | 至少 1、2；支持时加 4。 |
| source_count / client_count | 轻量基线规模。 |
| payload_bytes / payload_hz | 确定性 payload 大小和频率。 |
| local_fanout_write_count | publisher runtime 本地 fan-out。 |
| redis_publish_count | publisher runtime publish count 和 error count。 |
| redis_subscribe_message_count | subscriber runtime 收到的 Pub/Sub count。 |
| redis_remote_fanout_write_count | subscriber runtime remote fan-out。 |
| source/client payload match | 字节一致性。 |
| CPU/RSS | `Get-Process` 或等价采样。 |
| loop_delay_ms_p95 | 若 metrics 暴露；未暴露标 metric gap。 |
| slow_client_disconnect_count | 若支持慢客户端注入；未支持标 metric gap。 |

通过标准：

```text
self-test PASS。
单 Runtime NTRIP smoke deterministic payload 字节一致。
双 Runtime Pub/Sub smoke deterministic payload 字节一致。
Publisher Runtime local fan-out 与 Redis publish 可区分。
Subscriber Runtime redis_subscribe_message_count 和 redis_remote_fanout_write_count 增长。
Runtime A 停止 Runtime B 时本地 source/client 不受影响。
120 秒轻量容量无崩溃、payload mismatch、跨 mount 串流、Redis error 持续增长。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| deterministic payload mismatch 或跨 mount 串流 | S0 |
| Pub/Sub 使用旧 channel | S0 |
| Worker 计数和实际 metrics 不一致 | S1 |
| Redis publish/subscribe error 持续增长 | S1 |
| 轻量规模崩溃或数据面断流 | S1 |
| 未做 16k 正式容量 | 非失败，记录 out-of-scope |

## 13. P3-7 Integration Smoke

本 gate 只在 NC-106 运行。

前置条件：

```text
NC-100 到 NC-105 均 DEV_DONE。
NC-101/NC-102/NC-103/NC-104 的未运行项已归并。
Admin/Agent/Caster/Web 均来自同一 NC-106 commit。
PostgreSQL 和 Redis 均为真实 fixture。
Web build 环境缺口已闭合。
```

建议集成入口：

```powershell
.\deploy\scripts\v2_phase3_closed_loop_smoke.ps1 `
  -PostgresDsn $env:NAVCASTER_ADMIN_POSTGRES_DSN `
  -RedisAddr $env:NAVCASTER_ADMIN_REDIS_ADDR `
  -AdminPort 18080 `
  -WebPort 5173 `
  -RuntimeANtripPort 42195 `
  -RuntimeAHealthPort 19195 `
  -RuntimeBNtripPort 42196 `
  -RuntimeBHealthPort 19196 `
  -Mount QA_MOUNT_P3_CLOSED_LOOP `
  -EvidenceDir .\build\p3-qa\evidence
```

最小步骤：

```text
1. 清理旧进程、端口、PG 数据库、Redis v2:* key 和 build/p3-qa。
2. 启动 PostgreSQL 和 Redis fixture。
3. 构建 AdminService、Agent、navcaster-caster、Web。
4. 启动 AdminService，确认 health postgres=ok redis=ok。
5. 启动 Agent，确认 register/heartbeat。
6. 启动 Web dev/preview，打开真实浏览器。
7. Web 或 API 创建 Runtime A/B desired=running。
8. 验证 PG desired/control_intents/audit。
9. 验证 Redis projection 和 v2:control:config notify。
10. Agent reconcile 启动两个真实 navcaster-caster。
11. 验证两个 Caster health/metrics。
12. Agent 上报 runtime-events/runtime-metrics actual[]。
13. AdminService control API 聚合 desired+actual。
14. Web 显示 desired=running、actual=running、observed_desired_version 收敛。
15. 单 Runtime NTRIP source/client payload 一致。
16. 双 Runtime Redis Pub/Sub payload 一致。
17. 执行 restart 或 drain intent，验证 pending -> observed -> converged/failed。
18. 停 AdminService 30 秒，既有 Caster 数据流继续；恢复后 Agent 补传。
19. 停 Agent，Web/Admin 标 stale/offline，Admin 不直接杀 Caster。
20. Kill Caster，Agent 记录事件并按 restart_policy 处理。
21. 浏览器刷新后仍展示 live API 状态。
22. 清理 fixture、进程、端口、PG row、Redis v2:* key 和临时目录。
```

通过标准：

```text
完整 Web -> Admin -> PG/Redis -> Agent -> Caster -> Agent -> Admin -> Web 闭环可复现。
所有证据来自 v2 endpoint/key/table/program。
AdminService 离线不打断既有 Caster 数据面。
Agent 离线不被 AdminService 远程杀进程替代。
Caster 崩溃有 event 和 actual 状态变化。
清理后无测试进程、端口、容器、v2:* key、PG 测试 row 残留。
```

失败分级：

| 失败 | 分级 |
| --- | --- |
| 闭环任一核心边界只能用 mock/stub 代替 | S1 |
| Admin 离线导致既有数据面断流 | S1 |
| Agent 离线时 Admin 直接杀 Caster | S0 |
| Web convergence 与 Admin actual 不一致 | S1 |
| cleanup 残留真实运行进程或污染共享数据 | S2 |

## 14. P3-8 Reviewer Evidence Input

NC-106 必须为 NC-107 提供 evidence bundle：

```text
_team/qa/NC-106-v2-phase3-closed-loop-integration.md
build/p3-qa/evidence/commands.log
build/p3-qa/evidence/admin.log
build/p3-qa/evidence/agent.log
build/p3-qa/evidence/runtime-a.stdout.log
build/p3-qa/evidence/runtime-b.stdout.log
build/p3-qa/evidence/runtime-a.metrics.json
build/p3-qa/evidence/runtime-b.metrics.json
build/p3-qa/evidence/pg-consistency.sql.out
build/p3-qa/evidence/redis-consistency.txt
build/p3-qa/evidence/browser-desktop.png
build/p3-qa/evidence/browser-mobile.png
build/p3-qa/evidence/browser-console.txt
build/p3-qa/evidence/unrun-items.md
```

Reviewer 抽查清单：

```text
diff 是否破坏岗位边界。
PG source-of-truth 是否仍是 desired/control_intents/audit/actual/events 的长期事实。
Redis 是否只用 v2:* projection/cache/bus。
AdminService 是否避免远程执行。
Agent 是否只管理本机 start_token 匹配 runtime。
Caster 热路径是否不访问 PG/AdminService。
Web 是否只通过 AdminService v2 API 控制。
accepted/pending 和 actual converged 是否分开展示。
runtime_events 和 runtime_actual_snapshots 是否能解释真实进程生命周期。
Pub/Sub payload 和 counters 是否对应。
未运行项是否按阻断/非阻断分类。
```

## 15. Defect Severity

| 等级 | 定义 | 示例 |
| --- | --- | --- |
| S0 Critical | 数据错发、安全边界破坏、旧系统兜底通过、跨机器/跨 runtime 误操作。 | 跨 mount 串流；Web/Agent 直接写 Redis/PG；Agent 杀非本机进程；旧 ACT/MPT key 作为 v2 通过证据。 |
| S1 Blocker | Phase 3 闭环核心 gate 无法通过或状态语义错误。 | PG/Redis 不一致；actual 未收敛却显示成功；Admin 离线中断数据面；真实 Caster 无法启动。 |
| S2 Major | 影响可观测、恢复、审计或用户判断，但有窄范围替代证据。 | runtime_events 缺字段；cleanup 残留；Web stale 状态不明显；部分 metrics 缺失。 |
| S3 Minor / Env | 环境、文档、自动化或非阻断体验问题。 | `tsc.cmd` 缺失；浏览器自动化未安装但有手工截图；非关键 lint warning。 |

升级规则：

```text
任何旧 API/key/protobuf/Web 兼容兜底作为通过证据，直接升级 S0。
任何数据错发、跨 mount 串流、错误踢线或权限绕过，直接升级 S0。
环境缺口如果阻断必跑 gate 且无替代证据，结果为 QA_BLOCKED。
```

## 16. Unrun And Environment Gap Templates

未运行项表：

| 项目 | Gate | 原因 | 风险 | 后续任务 | 是否阻断当前结论 |
| --- | --- | --- | --- | --- | --- |
| 示例：Web browser smoke | P3-3 | `web\node_modules\.bin\tsc.cmd` 缺失，无法 build/dev | 无法证明真实浏览器 live API | NC-104 / NC-106 | 是，若在 NC-106 仍未闭合 |

环境缺口模板：

```markdown
### ENV_GAP: <name>

- 检查命令：
- 失败现象：
- 影响 gate：
- 已运行的较窄替代验证：
- 为什么替代验证不足以通过完整 gate：
- 后续补测任务：
- 是否阻断当前结论：
```

缺陷模板：

```markdown
### DEFECT <severity>: <title>

- Gate:
- Commit:
- 环境：
- 复现命令：
- 期望：
- 实际：
- 证据路径：
- 初步归因：
- 建议处理：
```

QA 记录模板：

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

## 环境

- OS:
- Go:
- CMake / Ninja / compiler:
- Node / npm:
- Docker:
- PostgreSQL:
- Redis:
- ports:

## 覆盖范围

| Gate | 结果 | 证据 |
| --- | --- | --- |
| P3-0 baseline readiness | PASS/FAIL/NR |  |
| P3-1 Admin PG/Redis | PASS/FAIL/NR |  |
| P3-2 Agent/Caster multi-runtime | PASS/FAIL/NR |  |
| P3-3 Web browser smoke | PASS/FAIL/NR |  |
| P3-4 control intent lifecycle | PASS/FAIL/NR |  |
| P3-5 Runtime events/actual tracking | PASS/FAIL/NR |  |
| P3-6 Caster workers/pubsub capacity | PASS/FAIL/NR |  |
| P3-7 integration smoke | PASS/FAIL/NR |  |
| P3-8 reviewer evidence input | PASS/FAIL/NR |  |

## 命令结果

| 命令 | 结果 | 说明 |
| --- | --- | --- |
| `git diff --check` | PASS/FAIL |  |

## PG / Redis Consistency

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| desired PG vs Redis projection | PASS/FAIL/NR |  |
| control_intents vs runtime_events | PASS/FAIL/NR |  |
| actual snapshots vs Redis actual TTL | PASS/FAIL/NR |  |
| worker-stat / mount-owner | PASS/FAIL/NR |  |

## Browser Evidence

- Desktop screenshot:
- Mobile screenshot:
- Console errors:
- Network evidence:
- Trace:

## 未运行项

| 项目 | Gate | 原因 | 风险 | 后续任务 | 是否阻断当前结论 |
| --- | --- | --- | --- | --- | --- |

## 缺陷

| Severity | Gate | 标题 | 状态 |
| --- | --- | --- | --- |

## Cleanup

- Processes:
- Ports:
- Docker containers:
- Redis keys:
- PostgreSQL rows:
- Temp dirs:
```

## 17. NC-106 Command Checklist

NC-106 可直接按以下顺序执行，遇到 S0/S1 失败先停止扩大测试。

```powershell
# P3-0
git status --short --branch
git diff --check

# Fixture
docker run --rm -d --name navcaster-p3-qa-pg -e POSTGRES_USER=navcaster -e POSTGRES_PASSWORD=navcaster -e POSTGRES_DB=navcaster_p3_qa -p 15432:5432 postgres:16
docker run --rm -d --name navcaster-p3-qa-redis -p 16379:6379 redis:8.6.3

# Admin
cd app\admin
go test ./...
go build -o .cache\bin\navcaster-admin.exe .\cmd\navcaster-admin
cd ..\..

# Agent
cd app\agent
go test ./...
go build -o .cache\bin\navcaster-agent.exe .\cmd\navcaster-agent
cd ..\..

# Caster
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 -NtripPort 42195 -HealthPort 19195 -Mount QA_MOUNT_P3_A -WorkerCount 2
.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 -RedisHost 127.0.0.1 -RedisPort 16379 -RuntimeANtripPort 42195 -RuntimeAHealthPort 19195 -RuntimeBNtripPort 42196 -RuntimeBHealthPort 19196 -Mount QA_MOUNT_P3_PUBSUB -WorkerCount 2

# Web
cd web
npm run build
npm run dev -- --host 127.0.0.1 --port 5173
cd ..

# Full integration, once script exists or equivalent manual orchestration is ready.
.\deploy\scripts\v2_phase3_closed_loop_smoke.ps1 -PostgresDsn $env:NAVCASTER_ADMIN_POSTGRES_DSN -RedisAddr $env:NAVCASTER_ADMIN_REDIS_ADDR -AdminPort 18080 -WebPort 5173 -RuntimeANtripPort 42195 -RuntimeAHealthPort 19195 -RuntimeBNtripPort 42196 -RuntimeBHealthPort 19196 -Mount QA_MOUNT_P3_CLOSED_LOOP -EvidenceDir .\build\p3-qa\evidence
```

Current script status:

| Script | Status |
| --- | --- |
| `deploy/scripts/build_ninja.ps1` | Exists. |
| `deploy/scripts/v2_caster_ntrip_smoke.ps1` | Exists. |
| `deploy/scripts/v2_caster_redis_pubsub_smoke.ps1` | Exists. |
| `deploy/scripts/v2_phase3_multi_runtime_smoke.ps1` | Expected for NC-102/NC-106 or manual equivalent. |
| `deploy/scripts/v2_phase3_closed_loop_smoke.ps1` | Expected for NC-106 or manual equivalent. |
| `deploy/scripts/v2_caster_capacity_baseline.ps1` | Expected for NC-103 or manual equivalent. |
