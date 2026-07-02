# NavCaster v2 QA Matrix

更新时间：2026-06-27

本文定义 NavCaster v2 阶段性质量门槛、最小集成 smoke、系统级闭环验证和容量基线口径。
v2 QA 只按 v2 契约判定，不把旧 HTTP API、旧 Redis key、旧 protobuf 或旧 Web 行为兼容作为通过标准。

## 适用范围

v2 目标程序和边界：

| 程序 | 语言 | QA 关注点 |
| --- | --- | --- |
| `navcaster-admin` | Go | HTTP/JSON 控制面、PostgreSQL 权威数据、Redis 投影、Agent 注册和 desired state |
| `navcaster-agent` | Go | 本机注册、心跳、desired state 拉取、配置渲染、Caster 进程守护和 reconcile |
| `navcaster-caster` | C++ | NTRIP 数据面、Runtime/Worker、Redis Pub/Sub、本地 fan-out、health/metrics |
| `app/web` | TypeScript / React | v2 控制台、Host/Runtime/Worker 视图、操作意图入口、浏览器 smoke |

不适用范围：

- 不要求兼容旧 `CasterService` 的 HTTP route、Redis key、protobuf、配置名或 UI 结构。
- 不在早期每个小变更后运行完整系统 smoke。
- 不用旧接口可用性替代 v2 契约验证。
- 不把容量压测作为骨架阶段每个任务的硬门槛，容量只在基础闭环形成后集中执行。

## 节奏规则

v2 前期按关键集成边界验证，避免在每个小变更后频繁冒烟测试。

| 阶段 | 触发点 | 必跑检查 | 不要求 |
| --- | --- | --- | --- |
| 设计和契约 | API、PG schema、Redis key、配置契约更新 | 文档一致性、契约路径、diff check | 产品构建、系统 smoke |
| 单模块骨架 | Admin/Agent/Caster/Web 单模块可编译 | 模块 build/test 或语法检查 | 跨模块联调 |
| 集成边界 | 两个模块第一次串联 | 最小边界 smoke，例如 Admin+PG+Redis、Agent+Admin、Agent+Caster | 完整 Web/NTRIP/容量矩阵 |
| 基础闭环 | Admin/Agent/Caster/Web 都具备 MVP | 集中系统级 smoke，按本文顺序执行 | 每个小提交重复全量矩阵 |
| DEV_DONE / Review | NC-082 至 NC-086 进入 DEV_DONE 或 NC-089 集成闭环 | QA 矩阵复验、容量基线、Reviewer 总审查 | 以旧接口兼容兜底 |

## 结果规则

| 结果 | 含义 |
| --- | --- |
| `QA_PASSED` | 当前任务验收标准和对应最低门槛均通过。 |
| `QA_PARTIAL` | 只覆盖部分范围，未覆盖项和风险已列明。 |
| `QA_BLOCKED` | 环境或依赖缺失导致最低门槛无法执行，且没有等价替代证据。 |
| `QA_FAILED` | 存在可复现失败、契约不一致或验收标准未达成。 |

文档/计划任务允许在未运行产品构建时使用 `QA_PASSED`，但必须明确范围是文档任务，且列出未运行产品测试的原因。

## 阶段性质量门槛

### Gate 0：v2 契约冻结

通过标准：

- v2 API route、PostgreSQL table、Redis key、配置项和程序边界有文档入口。
- 契约中明确 AdminService 不执行远程命令，Agent 只管理本机，Caster 不访问 PostgreSQL 热路径。
- QA 记录明确使用 v2 契约，不引用旧接口兼容作为通过证据。

最低命令：

```powershell
git diff --check
git status --short
```

### Gate 1：单模块可构建

通过标准：

- AdminService、Agent、Caster、Web 各自 build/test 命令明确且可在模块完成后运行。
- 任一模块改动不得要求其他未完成模块也必须运行。
- 失败时记录模块、命令、日志路径和阻断原因。

最低命令按模块见下文。

### Gate 2：关键边界 smoke

触发点：

- AdminService 首次连接 PostgreSQL/Redis。
- Agent 首次完成 register/heartbeat/desired-state。
- Agent 首次启动/停止 Caster。
- Caster 首次完成 health/metrics 或最小 source/client 数据路径。
- Web 首次调用 v2 Admin API。

通过标准：

- 每个边界只跑最小 smoke，证明契约连通和状态收敛。
- 不要求同时覆盖容量、故障恢复、所有页面和完整业务矩阵。

### Gate 3：基础闭环 smoke

触发点：

- Admin/Agent/Caster/Web MVP 均可启动。
- Admin 能写 desired state。
- Agent 能 reconcile 并启动 Caster。
- Caster 能提供 health/metrics 和最小 NTRIP。
- Web 能展示 Host/Runtime/Worker 状态和发起操作意图。

通过标准：

- 按“集中系统级 smoke 顺序”完整跑通。
- AdminService 停止不应中断既有 Caster 数据面。
- Agent 离线、Admin 离线、Caster 崩溃的基础语义有 smoke 或明确缺口记录。

### Gate 4：容量基线和系统级 QA

触发点：

- 基础闭环已通过。
- NTRIP source/client、RTCM decode、Redis publish、worker metrics 和慢客户端控制具备可观测指标。

通过标准：

- 形成至少一轮可复现容量报告。
- 指标包含连接规模、source/client 比例、RTCM decode 开关、loop delay、fan-out cost、Redis publish、慢客户端。
- 未达目标时不得写 `QA_PASSED`，应写 `QA_PARTIAL` 或 `QA_FAILED` 并列出瓶颈。

## Phase 2 真实控制面闭环准入矩阵

Phase 2 从 `feature/NC-089-v2-foundation-closed-loop-integration @ 9497a97` 继续，不以旧
HTTP API、旧 Redis key、旧 protobuf、旧 `CasterService` 或旧 Web 行为作为通过证据。

本轮目标是建立真实控制面闭环的准入标准：

```text
Web intent
-> AdminService PostgreSQL desired state
-> Redis projection
-> Agent desired polling / reconcile
-> Agent 启动真实 navcaster-caster
-> Caster health / metrics / NTRIP / Redis Pub/Sub
-> Agent actual / events / metrics
-> AdminService actual 聚合
-> Web desired / actual 收敛展示
```

本轮不做 16k 正式容量报告。Phase 2 只要求轻量容量口径记录，用于判断 NC-096/后续
容量深化是否具备可复现测量入口。

### Phase 2 环境准备

最低环境：

| 依赖 | 要求 | 未满足时记录 |
| --- | --- | --- |
| PostgreSQL | 真实 PostgreSQL fixture 或隔离测试库；记录版本、DSN 脱敏值、schema 来源和 migration commit。 | `PG_UNAVAILABLE`，列出是否退回内存 repository；真实闭环不得写通过。 |
| Redis | 真实 Redis fixture；记录版本、地址、是否同机、是否启用认证；只清理 `v2:*` 测试 key。 | `REDIS_UNAVAILABLE`，单模块可继续，PG/Redis 和 Pub/Sub gate 标为未运行。 |
| AdminService | `navcaster-admin` 构建产物或 `go run`；端口默认 `18080`。 | `ADMIN_UNAVAILABLE`，后续 Agent/Web 闭环 gate 不得通过。 |
| Agent | `navcaster-agent` 构建产物；本机可写状态目录和 runtime 工作目录。 | `AGENT_UNAVAILABLE`，闭环 gate 不得通过。 |
| Caster | `navcaster-caster` Release 构建产物；可分配 NTRIP 与 health/metrics 端口。 | `CASTER_UNAVAILABLE`，NTRIP、Pub/Sub 和闭环 gate 不得通过。 |
| Web | `npm run build` 可用；浏览器 smoke 优先 Playwright，缺失时记录手工步骤和截图路径。 | `BROWSER_AUTOMATION_UNAVAILABLE`，Web build 可通过但浏览器 smoke 标风险。 |

建议本地 fixture 端口：

```text
PostgreSQL 127.0.0.1:15432
Redis      127.0.0.1:16379
Admin      127.0.0.1:18080
Runtime A  NTRIP 42195, health 19195
Runtime B  NTRIP 42196, health 19196
Web dev    127.0.0.1:5173
```

环境准备命令口径：

```powershell
$env:NAVCASTER_PG_DSN = "postgres://navcaster:<redacted>@127.0.0.1:15432/navcaster_v2_qa?sslmode=disable"
$env:NAVCASTER_REDIS_ADDR = "127.0.0.1:16379"
$env:NAVCASTER_ADMIN_BASE_URL = "http://127.0.0.1:18080"

docker run --rm -d --name navcaster-v2-qa-pg `
  -e POSTGRES_USER=navcaster `
  -e POSTGRES_PASSWORD=navcaster `
  -e POSTGRES_DB=navcaster_v2_qa `
  -p 15432:5432 postgres:16

docker run --rm -d --name navcaster-v2-qa-redis `
  -p 16379:6379 redis:8.6.3
```

若 Docker 镜像、Docker Engine、PostgreSQL 或 Redis 不可用，QA 记录必须列出：

```text
缺失依赖
检查命令
失败现象
是否有较窄替代验证
对应阻断的 gate
后续补测任务
```

### Phase 2 Gate 列表

| Gate | 目标 | 可早期运行 | NC-097 必跑 | 准入结果 |
| --- | --- | --- | --- | --- |
| P2-0 环境与契约预检 | 确认真实 PG/Redis、端口、构建产物、v2 契约路径。 | 是，NC-095/各开发分支可运行文档和命令预检。 | 是。 | 环境缺失时后续对应 gate 标未运行或 blocked。 |
| P2-1 AdminService PG/Redis | AdminService 使用 PG source-of-truth，并生成 Redis projection。 | 是，NC-091 DEV_DONE 后可独立运行。 | 是。 | PG/Redis 未跑不能作为真实控制面通过。 |
| P2-2 Admin health/API | health、auth、agent register、desired-state、runtime control API 最小自检。 | 是，NC-091 后可运行。 | 是。 | API envelope、状态码、PG/Redis 状态必须符合 v2 契约。 |
| P2-3 Agent register/reconcile | Agent 注册、心跳、desired polling、启动/停止真实 Caster。 | 可用 stub Admin 早跑；真实 Admin 需 NC-091。 | 是。 | Agent 只管理本机 runtime，reconcile 幂等。 |
| P2-4 Caster 单 Runtime NTRIP | 单 Runtime source/client 确定性 payload 和 health/metrics。 | 是，NC-093 或 Caster 分支可运行。 | 是。 | payload 字节一致，metrics 计数收敛。 |
| P2-5 跨 Runtime Redis Pub/Sub | Runtime A source 经 Redis bus 到 Runtime B client。 | 是，NC-093 完成后可独立运行。 | 是。 | local fan-out 与 remote fan-out 计数可区分。 |
| P2-6 Web live API | Web 使用真实 AdminService API 展示 desired/actual 和 intent 状态。 | 可用 fixture 早跑；真实闭环需 NC-091/NC-092。 | 是。 | 不得依赖 mock 作为 NC-097 通过证据。 |
| P2-7 Admin-Agent-Caster-Web 闭环 | 完整控制面闭环，一条 runtime 创建/启动/观测/停止链路可复现。 | 否，必须等待第一波任务集成。 | 是。 | Phase 2 功能性准入核心 gate。 |
| P2-8 轻量容量记录 | 记录 worker/runtime/source/client/fan-out/pubsub/CPU/RSS。 | 可在 NC-093 后试跑单模块数据面。 | NC-097 后或 NC-096 执行。 | 只建基线，不承诺 16k。 |

### P2-0 环境与契约预检

前置环境：

- 当前 commit、来源基线和合入列表已记录。
- PostgreSQL/Redis fixture 或外部测试服务可访问。
- 测试端口无占用。
- 明确使用 `doc/design/v2-api-data-contract.md`、`doc/design/v2-control-plane-agent-runtime-contract.md`
  和本文作为准入依据。

命令口径：

```powershell
git status --short --branch
git diff --check
go version
node --version
npm --version
cmake --version
docker version
docker ps --format "{{.Names}} {{.Status}} {{.Ports}}"
```

通过标准：

- diff 范围符合任务边界。
- 没有产品源码误改或未说明的生成物缺口。
- PostgreSQL/Redis 连接、端口和 fixture 清理策略明确。
- QA 记录声明不使用旧接口/key/protobuf 作为 v2 通过证据。

未运行记录要求：

- 任一环境命令无法运行时，记录命令、错误、影响 gate 和替代方案。

### P2-1 AdminService 真实 PostgreSQL + Redis

前置环境：

- PostgreSQL schema 已 migration 到 Phase 2 所需版本。
- Redis fixture 中测试前 `v2:*` key 已清理或隔离到唯一 namespace。
- AdminService 使用真实 PG/Redis 配置启动，不能使用 `not_configured` health 作为本 gate 通过证据。

命令口径：

```powershell
cd app\admin
$env:GOCACHE = "$PWD\.cache\go-build-admin"
go test ./...
go build -o .cache\bin\navcaster-admin.exe .\cmd\navcaster-admin
.\.cache\bin\navcaster-admin.exe -config .\configs\qa.pg-redis.yml

curl.exe -fsS http://127.0.0.1:18080/api/v1/health
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/hosts
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/control/runtimes `
  -H "Content-Type: application/json" --data "@qa/fixtures/runtime-create.json"
redis-cli -h 127.0.0.1 -p 16379 --scan --pattern "v2:*"
```

推荐脚本口径：

```powershell
.\deploy\scripts\v2_admin_pg_redis_smoke.ps1 `
  -AdminBaseUrl http://127.0.0.1:18080 `
  -PostgresDsn $env:NAVCASTER_PG_DSN `
  -RedisAddr $env:NAVCASTER_REDIS_ADDR
```

通过标准：

- `/api/v1/health` 中 `postgres=ok`、`redis=ok`。
- Runtime desired state 写入 PostgreSQL source-of-truth。
- Redis projection 只包含 v2 key，payload 带 runtime_id、desired_state、config_version、version 或 checksum。
- `v2:control:config` notify 可订阅，创建或更新 Runtime desired state 后可收到 runtime / host desired projection 通知。
- 删除 Redis projection 后可从 PG 重建，或记录为 NC-091/NC-097 阻断缺口。
- operation/control intent 审计事实写入 PG。

未运行记录要求：

- 如果只跑内存 repository 或 `not_configured` health，结论只能写 `QA_PARTIAL`，并标记 `REAL_PG_REDIS_GAP`。

### P2-2 AdminService health/API self-check

前置环境：

- P2-1 AdminService 已启动或明确使用真实 PG/Redis 的测试实例。
- 有 bootstrap token 或测试认证配置。

命令口径：

```powershell
curl.exe -fsS http://127.0.0.1:18080/api/v1/health
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/auth/login `
  -H "Content-Type: application/json" --data "@qa/fixtures/admin-login.json"
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/register `
  -H "Content-Type: application/json" --data "@qa/fixtures/agent-register.json"
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/heartbeat `
  -H "Content-Type: application/json" --data "@qa/fixtures/agent-heartbeat.json"
curl.exe -fsS "http://127.0.0.1:18080/api/v1/agents/<agent_id>/desired-state?since_version=0"
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/runtimes/<runtime_id>
curl.exe -fsS http://127.0.0.1:18080/api/v1/control/events
```

通过标准：

- 所有成功响应使用 v2 envelope。
- action 类接口返回 `202` 或等价 `accepted`，不声称 Runtime 已完成。
- desired-state 空集和非空集都可解析，并支持 `since_version` 增量。
- runtime-events/runtime-metrics ingest 后，control API 可读到 actual/metrics。

未运行记录要求：

- API shape 只能用 fixture/mock 编译验证时，记录真实 AdminService 未跑原因和待 NC-097 补测项。

### P2-3 Agent register / reconcile / real Caster supervisor

前置环境：

- AdminService 可访问，或早期分支使用 contract stub 并明确标注。
- `navcaster-caster` 可执行文件路径固定。
- Agent 状态目录和 runtime 工作目录为空或唯一。

命令口径：

```powershell
cd app\agent
$env:GOCACHE = "$PWD\.cache\go-build-agent"
go test ./...
go build -o .cache\bin\navcaster-agent.exe .\cmd\navcaster-agent
.\.cache\bin\navcaster-agent.exe -config .\configs\qa.closed-loop.yml -once
```

推荐脚本口径：

```powershell
.\deploy\scripts\v2_agent_reconcile_smoke.ps1 `
  -AdminBaseUrl http://127.0.0.1:18080 `
  -CasterExe .\bin\Release\navcaster-caster.exe `
  -StateDir .\build\v2-agent-smoke
```

通过标准：

- register 生成或恢复 agent_id/host_id/secret_ref。
- heartbeat 包含资源指标和 runtime summary。
- desired=running 时 Agent 渲染本机配置并启动真实 `navcaster-caster`。
- desired=stopped 时只停止自己管理且 start_token 匹配的 runtime。
- 重复 reconcile 不重复启动进程。
- AdminService 短暂不可用时，Agent 不杀掉已有 Caster，并按 last-known desired state 继续守护。
- Agent 上报 actual-state/runtime-events/runtime-metrics 后 AdminService 可读到。

未运行记录要求：

- 若使用 dummy runtime 或 stub Admin，只能作为早期证据；NC-097 必须补真实 Admin + 真实 Caster。

### P2-4 Caster 单 Runtime NTRIP health/metrics

前置环境：

- third_party 已从本地主仓库 hydrate。
- `navcaster-caster` Release 构建成功。
- Runtime health/metrics 端口可访问。

命令口径：

```powershell
powershell -ExecutionPolicy Bypass -File F:\Projects\NavCaster\_team\scripts\HYDRATE_WORKTREE_SUBMODULES.ps1 `
  -WorktreePath <worktree>
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 -NtripPort 42195 -HealthPort 19195 -Mount QA_MOUNT_A
curl.exe -fsS http://127.0.0.1:19195/health
curl.exe -fsS http://127.0.0.1:19195/metrics
```

通过标准：

- source/client 均收到 `ICY 200 OK`。
- client 收到与 source 写入完全一致的确定性 payload。
- metrics 至少包含 runtime_id、worker_count、connection_count、source_count、client_count、
  fanout_write_count、redis_publish_count、slow_client_disconnect_count。
- health/metrics 不依赖 AdminService 存活。

未运行记录要求：

- C++ 构建、self-test、NTRIP smoke 三者任一未跑都要分别记录原因。

### P2-5 跨 Runtime Redis Pub/Sub smoke

前置环境：

- Redis fixture 可用。
- Runtime A 和 Runtime B 使用不同 NTRIP/health 端口。
- 两个 Runtime 使用同一 Redis bus，并有不同 runtime_id。

命令口径：

```powershell
.\deploy\scripts\v2_caster_pubsub_smoke.ps1 `
  -RedisAddr 127.0.0.1:16379 `
  -RuntimeANtripPort 42195 `
  -RuntimeAHealthPort 19195 `
  -RuntimeBNtripPort 42196 `
  -RuntimeBHealthPort 19196 `
  -Mount QA_MOUNT_PUBSUB
```

手工等价步骤：

```text
1. 启动 Runtime A 和 Runtime B。
2. Runtime A 打开 source mount=QA_MOUNT_PUBSUB。
3. Runtime A 打开本地 client，Runtime B 打开远端 client。
4. source 写入确定性 payload。
5. A 本地 client 与 B 远端 client 均收到完整 payload。
6. A metrics 中 local fan-out 和 redis publish 增量符合预期。
7. B metrics 中 redis subscribe/remote fan-out 增量符合预期。
8. Redis channel 使用 v2:stream:mount:<mount> 或 NC-090 冻结的 v2 bus 名称，不使用旧 channel。
```

通过标准：

- Runtime A/B 都可 health/metrics。
- remote fan-out 不是通过同进程共享内存或旧 Redis channel 实现。
- redis publish/subscribe error count 为 0。
- 停 Runtime B 不影响 Runtime A 本地 source/client。

未运行记录要求：

- Redis 不可用时标 `REDIS_PUBSUB_GAP`，并保留可执行命令和端口计划。

### P2-6 Web live API smoke

前置环境：

- Web build 依赖已安装。
- AdminService API 使用真实 `/api/v1/control/*` 和 v2 envelope。
- 真实闭环 smoke 中不允许只用 mock 数据通过。

命令口径：

```powershell
cd app/web
npm run build
npm run dev
```

推荐脚本或浏览器口径：

```powershell
.\deploy\scripts\v2_web_live_api_smoke.ps1 `
  -WebBaseUrl http://127.0.0.1:5173 `
  -AdminBaseUrl http://127.0.0.1:18080
```

浏览器步骤：

```text
1. 登录 v2 Web。
2. 打开 Host 列表，确认 agent online/offline、last heartbeat。
3. 打开 Runtime 列表，确认 desired_state、actual_state、config_version、observed_desired_version。
4. 打开 Runtime 详情，确认 worker_count、source/client、fan-out/pubsub、CPU/RSS 或可用指标。
5. 发起 start/stop/restart intent。
6. 页面先展示 accepted/pending/applying，再根据 actual state 收敛到 running/stopped/failed。
7. 刷新页面后状态仍来自 AdminService live API。
```

通过标准：

- TypeScript build 通过。
- Web 默认 live API 指向 v2 AdminService，不把 mock 作为生产路径。
- intent UI 不直接展示“远程执行成功”，必须展示 pending/actual 收敛。
- failed/stale/offline 状态可见。

未运行记录要求：

- 未跑浏览器时必须说明是否已有 API 证据，缺少截图或自动化标为风险。

### P2-7 Admin-Agent-Caster-Web 闭环 smoke

本 gate 必须等 NC-091、NC-092、NC-093、NC-094 集成到 NC-097 后运行。

前置环境：

- P2-1 至 P2-6 的单模块或边界 smoke 无阻断失败。
- AdminService、Agent、Caster、Web 全部来自同一 NC-097 commit。
- PostgreSQL 和 Redis 均为真实 fixture。

命令口径：

```powershell
.\deploy\scripts\v2_phase2_closed_loop_smoke.ps1 `
  -PostgresDsn $env:NAVCASTER_PG_DSN `
  -RedisAddr $env:NAVCASTER_REDIS_ADDR `
  -AdminPort 18080 `
  -WebPort 5173 `
  -RuntimeNtripPort 42195 `
  -RuntimeHealthPort 19195 `
  -Mount QA_MOUNT_CLOSED_LOOP
```

最小闭环步骤：

```text
1. 清理 fixture 数据、旧进程、旧端口和 v2 测试 key。
2. 启动 PostgreSQL、Redis、AdminService、Agent、Web。
3. Web 或 Admin API 创建 Runtime desired state，start_immediately=true。
4. 验证 PG desired state、control intent、audit log。
5. 验证 Redis projection 和 `v2:control:config` notify。
6. Agent 拉取 desired state，启动真实 navcaster-caster。
7. Caster /health 与 /metrics 正常。
8. Agent 上报 actual/events/metrics。
9. AdminService control API 聚合出 running actual state。
10. Web 展示 desired=running、actual=running、metrics 更新。
11. 单 Runtime NTRIP source/client payload 字节一致。
12. 执行 stop 或 restart intent，并验证 pending -> actual 收敛。
13. 停 AdminService 30 秒，既有 Caster 数据流继续；恢复后 Agent 补传 actual/events。
14. 清理所有进程、端口、PG fixture、Redis v2 测试 key 和临时目录。
```

通过标准：

- 一条 Runtime create/start/observe/stop 或 restart 链路可复现。
- Web 展示来自 AdminService 聚合后的真实 desired/actual，不是静态 mock。
- AdminService 离线不打断既有 Caster NTRIP 数据面。
- Agent 离线后 AdminService/Web 标记 stale/offline，不假设 Runtime 已停。
- Caster 崩溃后 Agent 按 restart_policy 处理并上报事件。

未运行记录要求：

- 该 gate 未跑时，NC-097 不能写完整 `QA_PASSED`；只能写 `QA_BLOCKED`、
  `QA_PARTIAL` 或 `QA_PASSED_WITH_NOTED_GAPS`，并说明是否存在等价证据。

### P2-8 轻量容量基线记录

本 gate 服务 NC-096 和 NC-098 风险判断，不是 16k 正式容量报告。

前置环境：

- P2-4 单 Runtime 或 P2-5 跨 Runtime 已通过。
- metrics 至少能采集 connection/source/client、worker_count、fan-out/pubsub、CPU/RSS。

建议命令口径：

```powershell
.\deploy\scripts\v2_capacity_baseline.ps1 `
  -RedisAddr 127.0.0.1:16379 `
  -RuntimeCount 1 `
  -WorkerCount 1,2 `
  -SourceCount 2 `
  -ClientsPerSource 3 `
  -DurationSec 120 `
  -SampleIntervalSec 10 `
  -OutDir .\build\v2-capacity-baseline
```

必须记录字段：

| 字段 | 说明 |
| --- | --- |
| commit | NC-097 或 NC-096 commit。 |
| runtime_count | 1 和可选 2；跨 Runtime 时记录每个端口和 runtime_id。 |
| worker_count | 至少 1；若支持动态 worker，记录 1/2 或 1/4 对比。 |
| source_count / client_count | 轻量建议 2 sources、每 source 3 clients；可按机器能力上调。 |
| fanout_write_count | 本地 fan-out 总数和速率。 |
| redis_publish_count / redis_subscribe_count | Pub/Sub 总数、速率、错误数。 |
| CPU | 采样方式，例如 `Get-Process`、Performance Counter、`typeperf` 或 Linux `pidstat`。 |
| RSS | Runtime、Agent、AdminService 分进程 RSS。 |
| loop_delay / latency | 若已暴露，记录 p50/p95；未暴露时记为 metric gap。 |
| duration | smoke 可 120s；不得写成长期 soak。 |

轻量容量通过标准：

- 120 秒内无崩溃、无 payload mismatch、无跨 mount 串流。
- metrics 可导出并能说明 runtime/worker/source/client/fan-out/pubsub 的对应关系。
- CPU/RSS 有可复现采样命令或脚本输出。

明确不通过标准：

- 不要求达到 16k 连接。
- 不要求 30 分钟或 2 小时 soak。
- 不以轻量容量数据承诺生产容量。
- 如果轻量规模就出现崩溃、错发、数据面断流或 Pub/Sub error 持续增长，应阻断 NC-098。

未运行记录要求：

- 若 NC-097 只做功能闭环未做容量，记录为 `P2_LIGHT_CAPACITY_NOT_RUN`，后续指向 NC-096。

## 模块最低验证矩阵

### AdminService

环境：

- Go toolchain。
- PostgreSQL fixture 或测试数据库。
- Redis fixture。
- v2 配置文件或环境变量。

最低命令：

```powershell
cd app\admin
go test ./...
go run ./cmd/navcaster-admin --config .\configs\qa.local.yml
```

API self-check：

```powershell
curl.exe -fsS http://127.0.0.1:18080/api/v1/health
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/register -H "Content-Type: application/json" --data "@qa/fixtures/agent-register.json"
curl.exe -fsS -X POST http://127.0.0.1:18080/api/v1/agents/<agent_id>/heartbeat -H "Content-Type: application/json" --data "@qa/fixtures/agent-heartbeat.json"
curl.exe -fsS http://127.0.0.1:18080/api/v1/agents/<agent_id>/desired-state?since_version=0
```

通过标准：

- `go test ./...` 通过。
- `/api/v1/health` 返回 v2 health payload。
- Agent register 生成或恢复 `agent_id` / `host_id`，不会写旧 key 作为权威数据。
- heartbeat 更新 Host/Agent actual state。
- desired-state 返回版本化 v2 desired state，空状态也必须是可解析的 v2 JSON。
- PostgreSQL 写入是长期权威，Redis 仅作为投影或运行态。

阻断项：

- Caster 热路径需要 AdminService 同步响应才能维持既有数据流。
- AdminService 直接 SSH 或远程执行进程命令。
- API 只实现旧 route，未提供 v2 route。

### Agent

环境：

- Go toolchain。
- 可访问 AdminService fixture。
- 本机可写 Agent 状态目录。
- 可启动 dummy runtime 或 `navcaster-caster` skeleton。

最低命令：

```powershell
cd app\agent
go test ./...
go run ./cmd/navcaster-agent --config .\configs\qa.local.yml
```

register/heartbeat/desired-state/reconcile smoke：

```text
1. 使用 bootstrap token 注册或恢复 agent identity。
2. 上报 heartbeat、CPU、内存、磁盘、网络和 runtime actual state。
3. 拉取 desired-state，记录 since_version 增量语义。
4. desired=running 时渲染配置并启动本机 runtime。
5. desired=stopped 时停止本机 runtime。
6. AdminService 短暂不可用时继续保持 last-known desired state。
7. AdminService 恢复后重新上报 actual state 并 reconcile。
```

通过标准：

- Agent 只管理本机 runtime，且只管理自己创建或登记的 runtime_id。
- 本地状态至少包含 agent_id、host_id、last_desired_version、runtime desired/actual 快照。
- reconcile 幂等，重复 desired-state 不应重复启动进程。
- 进程停止有超时和清理路径。

阻断项：

- Agent 直接修改 PostgreSQL 业务表。
- Agent 管理其他机器资源。
- AdminService 不可用时 Agent 杀掉已运行 Caster。

### Caster Runtime

环境：

- CMake + Ninja 或 v2 C++ 构建脚本。
- Redis fixture。
- 可用 NTRIP source/client 测试工具。
- 本机 health/metrics 端口。

最低命令：

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --config .\caster\configs\qa.local.yml
```

health/metrics smoke：

```powershell
curl.exe -fsS http://127.0.0.1:19080/health
curl.exe -fsS http://127.0.0.1:19080/metrics
```

最小 NTRIP smoke：

```text
1. 启动 1 个 Runtime，worker_count=1 或最小可用值。
2. 打开 source 到 mount=QA_MOUNT_A。
3. 打开 client 订阅 QA_MOUNT_A。
4. source 写入确定性 payload。
5. client 收到完整 payload，字节一致。
6. /metrics 中 connection_count、source_count、client_count、worker_count、redis_publish_count 增量符合预期。
7. Redis Pub/Sub 或 runtime projection 没有写入旧 key 作为 v2 通过证据。
```

双 Runtime Redis Pub/Sub smoke：

```powershell
.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 -RedisHost 127.0.0.1 -RedisPort 6379
```

检查点：

```text
1. 启动 Runtime A 和 Runtime B，共用同一个 Redis。
2. Runtime B 先打开 client 订阅同一 mount，metrics 出现 redis_subscribed_mount_count>=1。
3. Runtime A 打开 source 并写入确定性 payload。
4. Runtime B client 收到完全相同 payload。
5. Runtime A publisher worker 的 redis_publish_count>=1 且 redis_publish_error_count=0。
6. Runtime B subscriber worker 的 redis_subscribe_message_count>=1、redis_remote_fanout_write_count>=1 且 redis_error_count=0。
7. 只使用 `v2:stream:mount:<mount>`，不把旧 `MPT:<mount>` channel/key 作为通过证据。
```

通过标准：

- Runtime health 不依赖 AdminService 存活。
- Worker 独立 event loop 和 Redis context 的指标可见。
- 同 mount source/client 优先同 worker 本地 fan-out。
- Redis publish、subscribe 和 remote fan-out 可区分观测，错误计数为 0。
- 慢客户端阈值、output buffer 指标和断开计数可采集。

阻断项：

- Caster Runtime 直接访问 PostgreSQL 热路径。
- Worker 直接修改其他 Worker 的 session map。
- health/metrics 只能通过 AdminService 间接访问。
- 最小 source/client 不能传递确定性 payload。

### Web

环境：

- Node.js / npm。
- 可访问 AdminService fixture 或 mock server。
- 浏览器 smoke 工具，优先 Playwright；没有自动化时保留手工截图和步骤。

最低命令：

```powershell
cd app/web
npm ci
npm run lint
npm run build
npm run dev
```

浏览器 smoke：

```text
1. 登录 v2 管理台。
2. 打开 Host 列表，看到 Agent online/offline 状态。
3. 打开 Runtime 列表，看到 desired/actual state、config_version、worker_count。
4. 打开 Runtime 详情，看到 Worker metrics、loop delay、source/client 计数。
5. 发起 Create Runtime 或 Start/Stop/Restart/Drain 意图。
6. 页面展示 intent accepted / pending / applying / running / failed，不展示“本地直接执行成功”。
7. 刷新后状态仍来自 AdminService v2 API。
```

通过标准：

- v2 页面不依赖旧 NavCaster Web route 或旧 API shape。
- 操作通过 AdminService 写 desired state 或 action intent。
- 页面能展示 pending 和 failed 状态。
- TypeScript build 通过；v2 新页面不引入未解释 lint 债务。

阻断项：

- Web 直接访问 Redis、Caster 内部 session 容器或 Agent 本机接口来执行控制动作。
- Web 把操作显示为已完成，但 Admin/Agent/Caster actual state 未收敛。
- 只跑旧页面构建，不覆盖 v2 控制台入口。

## 集中系统级 Smoke 顺序

只在基础闭环形成后集中执行以下顺序。若中途失败，停止后续容量测试，先修复阻断项。

1. 环境准备
   - 清理旧进程、端口、PostgreSQL schema、Redis key 和临时配置。
   - 启动 PostgreSQL、Redis。
   - 记录 OS、CPU、内存、Go/CMake/Node 版本和 git commit。

2. AdminService 启动
   - 运行 migration。
   - 启动 AdminService。
   - 验证 `/api/v1/health`、登录、session 和基础权限。

3. Agent 注册和心跳
   - 启动 Agent。
   - 验证 register、heartbeat、Host online、资源指标。
   - 验证 desired-state 空集可解析。

4. 创建 Runtime desired state
   - Web 或 Admin API 创建 runtime。
   - 验证 PostgreSQL desired state、config_version、审计日志。
   - 验证 Redis 配置投影仅作为投影。

5. Agent reconcile 启动 Caster
   - Agent 拉取 desired state。
   - 渲染本机配置。
   - 启动 `navcaster-caster`。
   - 上报 actual state、process_id、listen_port、worker_count。

6. Caster health/metrics
   - 验证 Runtime local `/health` 和 `/metrics`。
   - 验证 AdminService 聚合后的 Runtime/Worker 视图。
   - 验证 Web 展示 Runtime running 和 Worker 指标。

7. 最小 NTRIP 数据路径
   - 打开 source/client。
   - 发送确定性 payload。
   - 验证 client 收包、metrics 增量、Redis publish 增量。

8. 操作意图和收敛
   - 执行 restart 或 worker_count 增加。
   - 验证 Web 显示 pending/applying/running。
   - 验证 Agent reconcile 幂等。
   - 如实现 draining，验证 draining worker 不再接新 mount，旧连接自然退出。

9. 故障语义 smoke
   - 停 AdminService：既有 Caster 数据流继续，Agent 按 last-known desired state 守护。
   - 恢复 AdminService：Agent 上报 actual state 并 reconcile。
   - 停 Agent：Caster 进程不应被 AdminService 直接杀死，Host 标记 agent_offline。
   - 杀 Caster：Agent 按 restart_policy 拉起并上报事件。

10. 清理检查
    - 停止 Web/Admin/Agent/Caster。
    - 清理 fixture 数据。
    - 检查无残留进程、端口占用、临时配置和测试 Redis key。

## 容量基线口径

容量基线只在 Gate 4 执行。第一轮目标不是追求最终上限，而是建立可复现、可比较的 v2 数据面基线。

### 必报环境

| 字段 | 示例 |
| --- | --- |
| commit | `feature/NC-0xx @ <hash>` |
| OS / kernel | Windows Server / Linux kernel version |
| CPU | 型号、核心数、是否固定 affinity |
| 内存 | 总量、可用量 |
| build | Release、编译器、优化选项 |
| runtime_count | 1 / 2 / N |
| worker_count | 1 / 4 / 8 / 16 |
| Redis | 单机、Cluster、版本、是否同机 |
| PostgreSQL | 版本、是否参与热路径 |
| RTCM decode | off / sampled / full |
| test duration | 5 min smoke / 30 min baseline / 2 h soak |

### 必测场景

| 场景 | 目标 |
| --- | --- |
| 连接规模阶梯 | 1k、4k、8k、16k 总连接，失败时记录最后稳定点 |
| source/client 比例 | 1:1、1:3、1:10，主报告以 1:3 为默认容量口径 |
| 热 mount | 1 source 对 100、1000、5000 clients，观察单 mount fan-out |
| RTCM decode 开关 | off 与 sampled/on 对比 CPU、loop delay、吞吐 |
| worker_count 扩展 | 1、4、8 worker 下连接分布和 loop delay |
| Redis publish | 每 mount 数据速率乘订阅 Runtime 数，记录 publish 延迟和错误 |
| 慢客户端 | 注入限速 client，验证 output buffer、断开计数和不拖垮正常 client |
| Admin 离线窗口 | 容量测试中停 AdminService，验证数据面不受影响 |

### 必报指标

| 指标 | 采样窗口 | 说明 |
| --- | --- | --- |
| `connection_count` | 10s / 60s | 总连接、source、client 拆分 |
| `mount_count` | 10s / 60s | active mount 和 owner worker 分布 |
| `loop_delay_ms` | p50 / p95 / p99 | Runtime 和每 Worker 分开报 |
| `fanout_cost_us` | p50 / p95 / p99 | 每 mount 和全局聚合 |
| `redis_publish_latency_ms` | p50 / p95 / p99 | publish 调用或端到端 pub/sub 延迟 |
| `redis_publish_count` | rate | 每秒 publish 数和失败数 |
| `send_bps` / `recv_bps` | 10s / 60s | Runtime、Worker、mount 维度 |
| `rtcm_decode_cost_us` | p95 | decode off/sampled/on 对比 |
| `slow_client_disconnect_count` | count/rate | 慢客户端隔离效果 |
| `rss_memory_mb` | 10s / 60s | Runtime 和 Agent/Admin 分开报 |
| `cpu_percent` | 10s / 60s | 总 CPU 和单核心热点 |

### 初始容量判定

第一轮容量报告应至少给出：

```text
stable_connection_count
stable_source_count
stable_client_count
worker_count
rtcm_decode_mode
loop_delay_ms_p95
fanout_cost_us_p95
redis_publish_latency_ms_p95
slow_client_disconnect_count
failure_point 或 none
```

若 16k 连接、1:3 source/client、RTCM sampled、30 分钟运行无法稳定，需要记录瓶颈和资源上限，不得用短时 5 分钟 smoke 写成容量通过。

## 阻断项、报告项和环境缺口

### 阻断项

- 任何模块最低 build/test 失败。
- v2 API/Redis/PG 契约未实现，却用旧接口通过。
- AdminService 故障导致既有 Caster 数据面断流。
- Agent 离线或 Admin 离线语义与设计相反。
- Caster 最小 NTRIP source/client 无法传递确定性 payload。
- Caster 热路径访问 PostgreSQL。
- Web 控制动作绕过 AdminService。
- 容量测试中出现数据错发、跨 mount 串流、未隔离慢客户端导致正常 client 大面积阻塞。

### 报告项

- 第三方库编译 warning。
- Web chunk size、bundle size、非阻断 lint warning。
- 低于目标但已明确标为容量基线的性能数据。
- Docker/Redis/PostgreSQL fixture 启停耗时。
- 浏览器 smoke 的手工截图缺失，但已有 API 证据时标为风险。

报告项不得掩盖阻断项。

### 可接受环境缺口

仅在记录清楚时可接受：

- 本机无 PostgreSQL/Redis/Docker：文档或单元测试任务可继续，但集成 smoke 标为未运行。
- 无真实第二主机：可用本地多进程或 Docker bridge 作为替代证据，但必须标注可信度。
- 无浏览器自动化：可用手工步骤和截图替代一次，后续必须补 Playwright 或等价自动化。
- 无容量压测工具：基础闭环可通过，Gate 4 容量基线必须标为 blocked 或 pending。

## QA 记录模板

```markdown
# <Task ID> QA

## 结论

`QA_PASSED` / `QA_PARTIAL` / `QA_BLOCKED` / `QA_FAILED`

## 验证环境

- 日期：
- 工作目录：
- 分支 / commit：
- OS：
- Go / CMake / Node：
- PostgreSQL：
- Redis：
- Docker：

## 验证范围

- 覆盖的 v2 契约：
- 未覆盖的契约：

## 命令结果

| 命令 | 结果 | 说明 |
| --- | --- | --- |
| `git diff --check` | PASS/FAIL |  |

## 手工验证

- 步骤：
- 结果：
- 证据路径：

## 未运行项

| 项目 | 原因 | 风险 | 后续任务 |
| --- | --- | --- | --- |

## 阻断项

- 无 / 列表

## 备注

- 环境缺口：
- 已知 warning：
```

## 后续脚本化建议

当前 NC-087 不创建低成熟度脚本，避免依赖尚未完成的 v2 实现。基础闭环形成后建议新增：

```text
deploy/scripts/v2_admin_self_check.ps1
deploy/scripts/v2_agent_reconcile_smoke.ps1
deploy/scripts/v2_caster_ntrip_smoke.ps1
deploy/scripts/v2_system_smoke.ps1
deploy/scripts/v2_capacity_baseline.ps1
```

脚本必须满足：

- 默认只使用 v2 route、v2 Redis key 和 v2 程序名。
- 参数化端口、数据目录、PostgreSQL DSN、Redis endpoint。
- 成功和失败路径都清理临时进程和 fixture 数据。
- 支持 `-WhatIf` 或 dry-run 输出前置检查。
