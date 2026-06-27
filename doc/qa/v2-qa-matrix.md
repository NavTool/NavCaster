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
| `web` | TypeScript / React | v2 控制台、Host/Runtime/Worker 视图、操作意图入口、浏览器 smoke |

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

## 模块最低验证矩阵

### AdminService

环境：

- Go toolchain。
- PostgreSQL fixture 或测试数据库。
- Redis fixture。
- v2 配置文件或环境变量。

最低命令：

```powershell
cd admin
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
cd agent
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
cd web
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
