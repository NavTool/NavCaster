# NavCaster 项目记忆

生成时间：2026-06-12
基线复核：2026-06-16

当前团队基线：`team-dev`，以 Git 最新提交和 `roadmap/iteration-progress.md` 最近记录为准。

说明：本文是当前项目入口记忆。若和源码、`current/workflow.md` 或
`roadmap/iteration-progress.md` 冲突，以源码和最近迭代记录为准。

## 项目定位

NavCaster 是一个 C++ NTRIP Caster 服务，围绕 Redis 做集群状态、账号鉴权、连接状态、数据订阅发布、主节点选举和转发任务编排；同时内置 HTTP API 与 React 管理台。

默认运行面：

- NTRIP 服务端口：`4202`
- HTTP/API/Web 端口：`8080`
- Redis：模板默认 `127.0.0.1:16379`，密码 `password`
- 默认 Web 管理员：`admin/admin`
- 主二进制：`CasterService`

## 仓库地图

- `CMakeLists.txt`：顶层构建入口，设置三方库、版本信息、输出目录，并加入 `proto`、`src`、`tools`。
- `cmake/*.yml.in`：运行时配置模板，构建后生成到 `bin/<BuildType>/conf/`。
- `src/base`：基础工具，包括 NTRIP 报文、Base64、NMEA/RTCM 解析、系统资源采集、网络工具等。
- `src/core`：Caster 核心状态机与 Redis 模型，提供 `CASTER::*` API。
- `src/auth`：账号鉴权、在线记录、匿名登录、鉴权广播，提供 `AUTH::*` API。
- `src/http`：HTTP server、REST API handler、Redis adapter、SSE manager、内存环形日志视图。
- `src/service`：服务入口、配置加载、NTRIP listener、连接队列、session 和 relay 编排。
- `proto/caster`：`.proto` 源文件；`proto/src`：已提交的 `.pb.cc/.pb.h`，当前 CMake 直接消费生成物，不在构建时自动生成。
- `web`：Vite + React + TypeScript + Ant Design 管理台。
- `tools`：NTRIP client/server 模拟器、`strsvr_mult`、`schema_smoke` 和 API 契约检查等联调/验证工具。
- `deploy`：CI 打包、Docker runtime、systemd/supervisor/nssm 脚本和 smoke test。
- `doc`：唯一仓库文档入口，按当前事实、API、部署、QA、目标方向、参考资料和历史归档分区；部署故障处理入口见 `deployment/ops-runbook.md`。
- `third_party`：子模块形式的 abseil、protobuf、hiredis、libevent、yaml-cpp、rtklib、spdlog、json。

## 构建与打包

后端构建：

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release
```

Linux：

```bash
BUILD_TYPE=Release bash deploy/scripts/build_ninja.sh
```

Windows 打包：

```powershell
powershell -ExecutionPolicy Bypass -File deploy\scripts\package_windows.ps1
```

Linux 打包：

```bash
bash deploy/scripts/package_linux.sh
```

前端：

```bash
cd web
npm ci
npm run dev
npm run build
```

打包脚本会先检查并尽量补全构建环境，然后执行 contract check、Web build、CMake/Ninja 构建、
schema_smoke CTest 和发布目录组装。产物输出到 `dist/<PackageName>/`，并生成
`dist/<PackageName>.zip` 或 `dist/<PackageName>.tar.gz`。不传参数时脚本默认使用
Release、`dist/`、最新 Git tag 加距 tag 提交数或项目版本兜底值，并自动推断平台，因此包目录和归档
默认带版本号。脚本还会写出 `dist/package-metadata.env` 供 CI 上传实际产物。Linux 打包
脚本还会拉取并构建 Redis；CI 的 Ubuntu 20.04 包通过 `PACKAGE_PLATFORM=ubuntu-20.04-amd64`
固定平台后缀。

Docker runtime image：

```bash
bash deploy/scripts/build_runtime_image.sh
```

runtime image tag 使用 `navcaster:team-dev-<short12>`，并写入
`org.opencontainers.image.revision`、`version`、`source`、`created` 等 OCI
labels。`docker-compose.yml` 不再默认使用 `navcaster:latest`；启动 compose 前需
显式设置脚本输出的 `NAVCASTER_IMAGE`，例如：

```bash
export NAVCASTER_IMAGE=navcaster:team-dev-<short12>
docker compose -f deploy/docker/docker-compose.yml up -d
```

默认 package 仍可通过 `NAVCASTER_PACKAGE` 覆盖，但 QA 证据必须记录 image tag、
image id 和 revision label。

Smoke test：

```bash
BASE=http://127.0.0.1:8080 USER=admin PASS=admin bash deploy/scripts/e2e_smoke.sh
```

Windows 本地一体化 smoke 可自动启动 `redis:8.6.3` fixture、临时改写
`bin/<config>/conf/*.yml`、启动 `CasterService` 并验证 health/login/status/cluster：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release
```

活跃账号读侧可追加 `-IncludeActiveAccounts`；活跃账号运行中 SSE 增量可追加
`-IncludeActiveAccountSseDelta`；真实 NTRIP/Auth 写侧实名会话可追加
`-IncludeNtripAuthSession`；真实连接续期长跑可追加
`-IncludeNtripAuthSessionRenewal`；`Online_Protection` 连接数矩阵可追加
`-IncludeNtripOnlineProtection -NtripOnlineProtectionScenario RejectNew|KickOld`；
匿名登录矩阵可追加
`-IncludeNtripAnonymousAuth -NtripAnonymousScenario AllowAnonymous|RejectAnonymous`；
本地双实例 AUTH:BROADCAST 可追加 `-IncludeNtripAuthBroadcast`；本地双实例
node identity / cluster 可追加 `-IncludeLocalDualNodeIdentity`；
禁用/失效账号矩阵可追加 `-IncludeNtripDisabledAccount`；HTTP Redis 断线/重连
可追加 `-IncludeRedisReconnect`；本地真实 relay pull/push 控制链路可追加
`-IncludeRelayPullStartStop` 或 `-IncludeRelayPushStartStop`；master lease
可追加 `-IncludeMasterLeaseFailover` 或 `-IncludeMasterLeaseStability`；Docker
bridge cluster、HTTP ingress 和 relay failover smoke 见 `deployment/ops-runbook.md`。
必要时用 `-NtripPort` 避开本机端口冲突。

## 后端启动链路

入口是 `src/service/main.cpp`：

1. `ntrip_config::Init(argc, argv, "conf/")` 解析 `-port`、`-conf`，切换工作目录到可执行文件目录，并读取三个 YAML。
2. 读取 `Service_Setting.yml`、`Caster_Core.yml`、`Auth_Verify.yml`。
3. 初始化 spdlog sink，包含一个最近 5000 条日志的 ring buffer，供 `/api/logs/ring` 读取。
4. Windows 下执行 `WSAStartup`。
5. 注册 SIGTERM/SIGINT，进入 `ntrip_caster::start()`。

`ntrip_caster` 当前有两个 event loop：

- `_base`：NTRIP、Caster core、Auth、session、relay、队列。
- `_http_base`：HTTP API 与 SSE，运行在独立线程。

这一点和 `archive/historical/data-flow-and-architecture.md` 中“单 event_base”的描述不一致，以源码为准。

HTTP 启动策略：

- 默认只有 Redis 选出的主节点开启 HTTP API。
- `HTTP_API_Setting.Force_Enable` 如果在配置里设置为 true，则绕过主节点判断直接开启。
- 当前配置模板显式写出 `Force_Enable: false`。部署契约见 `deployment/http-ingress.md`。
- 当前 HTTP listener 一旦启动不会因后续 Master 丢失自动关闭。
- NC-040 产品化决策继续支持固定管理入口或稳定 sticky session 管理入口；普通
  round-robin 写入口池不属于当前支持能力，设计取舍见
  `design/http-multi-entry-productization.md`。

Redis 部署策略：

- 生产最低版本为 Redis Open Source 8.4.0+。
- 当前 Docker、CI 和 Linux package 默认 Redis 版本为 8.6.3。
- 关键命令依赖：`HSETEX`、`HEXPIRE`、`SET ... IFEQ ... EX`。
- 部署前用 `deploy/scripts/check_redis_compat.sh` 或 `.ps1` 校验外部 Redis。
- 本地任务 worktree 验证优先用 `deploy/scripts/e2e_smoke.ps1 -RedisMode Docker`
  拉起一次性 Redis fixture；若 Docker engine 或外部 Redis 不可用，QA 记录必须
  明确环境缺口。

## NTRIP 连接流

核心文件：

- `src/service/ntrip_listener.*`
- `src/service/connect_bev.*`
- `src/service/process_queue.*`
- `src/service/session/*`
- `src/service/ntrip_caster.*`

主流程：

1. `ntrip_listener` 接受 TCP，创建 bufferevent。
2. 解析首行和 header，识别 `SOURCE`、`GET`、`POST`。
3. `GET /` 走源表请求；`GET /mount` 走普通 rover 或 nearest；`POST`/`SOURCE` 走 base/server。
4. 解析 Basic Auth、`Ntrip-GGA`、`STR`、`User-Agent`、`Ntrip-Version` 等信息。
5. 调用 `AUTH::Verify`，成功后把 `ConnectInfo` 推入 `QUEUE`。
6. `ntrip_caster::process_request` 根据 `ConnectType` 创建 session 或销毁 session。

session 职责：

- `server_ntrip`：基准站登录，注册 mountpoint，读取 RTCM 原始数据并通过 `CASTER::Pub_Raw_Data` 发布。
- `client_ntrip`：移动站登录，订阅 mountpoint 数据并写回客户端，也会上传 rover 侧 NMEA/GGA。
- `client_near`：nearest rover，根据坐标订阅最近基站，可切换挂载点。
- `source_ntrip`：一次性返回 sourcetable，输出完成后关闭连接。
- `relay_pull`：从远端 caster 拉流，作为本地发布者注册。
- `relay_push`：订阅本地数据并推送到远端 caster。

`connect_bev` 统一管理 `connect_key -> bufferevent`，以及读写超时 timer。`process_queue` 是 main event loop 内部队列，目前没有锁，使用前要保持线程模型清晰。

## Caster Core 与 Redis

外部入口在 `src/core/include/Caster_Core.h` 的 `CASTER` namespace：

- `Init`/`Free`
- `Set_Node_Runtime_Info`
- `Is_Master_Node`
- `Register_Record`/`Withdraw_Record`
- `Pub_Raw_Data`
- `Sub_Raw_Data`/`Sub_Near_Raw_Data`/`Unsub_Raw_Data`
- `Get_Source_Table_Text`
- relay callback 与 pull/push 状态更新
- 坐标、延迟、状态、sourcetable、grid 等辅助接口

核心实现和 Redis key 主要在 `src/core/src/caster_internal.h/.cpp`。重要 key 前缀：

- 集群：`CASTER:MASTER`、`CASTER:NODE`
- 挂载点与用户：`MPT:*`、`USR:*`、`STR:*`
- 配置记录：`MPT:RECORD`、`ALIAS:RULE`、`ACCESS:GROUP`、`ACCESS:ITEM:*`
- relay：`PULL:RECORD`、`PULL:STAT`、`PUSH:RECORD`、`PUSH:STAT`
- 历史：`LOG:MPT:*`、`LOG:USR:*`、`LOG:NODE:*`、`NODE:HISTORY:*`

主节点通过 `CASTER:MASTER` 做 `SET NX EX` 抢占和 `SET ... IFEQ ... EX` 续租。
NC-011 后，续租成功/失败后的本地 master 状态转移、`master_acquired` /
`master_lost` 事件 JSON 和是否触发 cluster sync 由
`src/core/context/services/master_lease_service.*` 的纯逻辑规划；Redis 命令顺序仍保留在
`caster_internal` 回调中。主节点还负责 relay 分配、节点状态汇总、访问组/别名/nearest 等全局状态维护。

NC-012 后，`RelayScheduler` 的纯逻辑测试锁定 relay 分发幂等边界：record/status/distributed
一致时不发布动作；enabled record 尚无 status 时 ACTIVE 可重试以抗丢广播；orphan 或 disabled
status 尚存在时 INACTIVE 可重试；配置变化保持两轮式，先 INACTIVE 并清 distributed，待 status
消失后再 ACTIVE。真实 Redis callback 顺序、`PUBLISH NODE:<node_id>` 和 `_relay_cb` I/O 仍由
`caster_internal` 保持原时序。

## Auth 模块

入口在 `src/auth/include/Auth_Verify.h`：

- `AUTH::Init`
- `AUTH::Verify`
- `AUTH::Add_Login_Record`
- `AUTH::Add_Logout_Record`

实现使用 hiredis async，维护发布和订阅连接，并订阅 `AUTH:BROADCAST`。账号、在线、匿名用户相关 key 在 `src/auth/src/auth_verify_internal.cpp` 中，存在 `ACT:ACTIVE`、`ACT:REC:*`、`ACT:UND:*`、`ACT:UNNAMED` 等历史命名。

NC-007 已确认并修复 Auth 在线保护语义：

- `Rover_Setting["Online_Protection"]` 必须写入 `set_rover_online_protection(...)`，
  不能覆盖 `rover_anonymous_login`。
- `ACT:REC:<account>` 是实名连接数限制桶；`ACT:UND:<name>` 是匿名连接桶。
- `Online_Protection=true` 时已在线连接优先，新连接超过连接数上限会被拒绝。
- `Online_Protection=false` 时允许新连接挤掉最早的旧实名连接。

NC-008A 开始，实名 Auth 登录生命周期额外维护展示会话桶：

- key 为 `ACT:SESSION:<account>`，field 为 `connect_key`。
- value 是 JSON：`uid/connect_key/account/anonymous/auth_type/online_time/update_time/addr/port/group_uid`。
- 只有实名登录通过连接数限制后才写入；拒绝登录、连接数踢线、登出会删除对应 field。
- 定时续期只刷新已放行的实名会话，匿名登录暂不写入 `ACT:SESSION:*`。
- NC-008B 后 `/api/accounts/active` 和 SSE `account_actives` 统一调用 `AccountRepository::list_active_sessions()`，
  优先聚合 `ACT:SESSION:*`，并合并 legacy `STR:ACTIVE` fallback；冲突时新 session 优先。

`schema_smoke` 已覆盖 Auth 配置解析组合、Redis 连接字段解析、实名连接数策略纯逻辑、
`ACT:SESSION:*` JSON 契约，以及活跃账号 REST/SSE 读侧的新旧源兼容。NC-017 后，
Windows `deploy/scripts/e2e_smoke.ps1 -IncludeNtripAuthSession` 还会用 Redis 8.6.3
fixture 打开真实 NTRIP POST source 和实名 GET client，验证 Auth/Core 写入
`ACT:SESSION:<account>`、`/api/accounts/active` 可读取，并在 client 断连后清理
对应 `connect_key`。NC-018 后，`-IncludeNtripOnlineProtection` 会在独立服务
生命周期中验证 `Online_Protection=true` 拒绝新同账号连接、`false` 踢掉旧同账号
连接，并同时断言 `ACT:SESSION:<account>`、`ACT:REC:<account>`、`USR:REC:<account>`
最终只保留预期 `connect_key`。NC-019 后，`-IncludeNtripAuthSessionRenewal`
会保持真实实名 client 在线跨过续期窗口，断言 `ACT:SESSION:<account>` 同 field
的 `update_time` 增长，并用 `HTTL` 确认 `ACT:SESSION`、`ACT:REC`、`USR:REC`
三处 field TTL 仍为正；断连后继续确认三处 field 被清理。NC-021 后，
`-IncludeNtripAnonymousAuth` 会分别验证 `Rover_Setting.Anonymous_Login=true`
时无 Basic Auth rover 写入并清理 `ACT:UND:<name>`、不进入 `ACT:SESSION:*`
和 `/api/accounts/active`，以及 `false` 时无 Basic Auth rover 被拒绝且不残留
`ACT:UND/ACT:SESSION/ACT:REC/USR:REC`。NC-022 后，`-IncludeNtripAuthBroadcast`
会在同一 Redis fixture 下启动两个本地 `CasterService` 进程，验证 Node B
同账号新连接触发 `AUTH:BROADCAST` 关闭 Node A 旧连接，并确认
`ACT:SESSION/ACT:REC/USR:REC` 和两实例 `/api/accounts/active` 最终只保留
Node B connect_key。
NC-023 后，`-IncludeNtripDisabledAccount` 会通过 HTTP 账号 API 创建 enabled
账号，验证真实 NTRIP Basic Auth client 可登录；随后依次更新 frozen、inactive
和 expired，确认 `ACT:ACTIVE` 删除、NTRIP client 被拒绝关闭，且
`ACT:SESSION/ACT:REC/USR:REC` 与 `/api/accounts/active` 不残留该账号。

## HTTP API

HTTP 层文件：

- `src/http/http_server.*`：libevent HTTP wrapper、路由匹配、CORS、鉴权、静态文件。
- `src/http/http_handler.*`：具体 REST API、登录 token、审计日志、配置、统计、monitor、SSE。
- `src/http/redis_adapter.*`：HTTP event loop 上的 Redis async adapter。
- `src/http/sse_manager.*`：定时拉取 Redis hash，变化时推送 SSE。
- `src/http/ring_log_view.*`：进程内日志 ring buffer。

公开路径：

- `POST /api/auth/login`
- `GET /api/status/health`
- `GET /api/events/stream?token=...&channels=...`

登录 token 是进程内 map，不是 JWT，也不会持久化；服务重启后前端 token 会失效。
该 token 不跨节点共享，SSE 也使用同一认证边界；多入口部署必须使用固定或 sticky
管理入口，不能把多个 HTTP 节点当作无状态 round-robin 写入口。

主要 API 分组：

- 账号：`/api/accounts`、`/api/accounts/active`
- 源表/挂载点：`/api/sources`、`/api/utils/sourcetable/*`
- 在线连接：`/api/servers`、`/api/clients`、`/api/streams`
- 踢下线：`/api/servers/kick/*`、`/api/clients/kick/*`
- 别名与访问控制：`/api/aliases`、`/api/access/groups`、`/api/access/items/*`
- 转发：`/api/relays/pull`、`/api/relays/push`、start/stop/status
- 集群：`/api/nodes`、`/api/nodes/history/*`
- 统计和历史：`/api/logs/*`、`/api/stats/*`
- 运维：`/api/status`、`/api/config`、`/api/monitor/*`、`/api/audit`、`/api/logs/ring`、`/api/system/events`、`/api/nodes/log-level/*`

SSE：

- endpoint：`/api/events/stream`
- token 通过 query string 传入。
- 默认 2 秒轮询一次 Redis 数据。
- 默认最大 200 个 SSE client。
- 已注册 channel 包括 `servers`、`clients`、`streams`、`nodes`、`accounts`、`sources`、`aliases`、`access_groups`、`pull_records`、`pull_states`、`push_records`、`push_states`、`account_actives`。

## 前端

技术栈：Vite + React 18 + TypeScript + React Router + Ant Design + Axios + Recharts。

关键文件：

- `web/src/api/client.ts`：Axios 实例、baseURL/token localStorage、Bearer 注入、401/403 跳登录。
- `web/src/api/auth.ts`：登录登出。
- `web/src/api/resource.ts`：通用 Redis hash CRUD API helper。
- `web/src/api/index.ts`：业务 API 聚合。
- `web/src/api/types.ts`：前端类型，与 proto/HTTP JSON 字段保持一致。
- `web/src/hooks/useSSE.ts`：EventSource 封装，支持单 channel 和多 channel，断线指数退避重连。
- NC-009 后 `useSSE(channel)` 默认请求 `channels=<channel>`，服务端 SSE 定时器只轮询当前有订阅者的频道；`channels=*` 仍保留订阅全部语义。
- `web/src/hooks/usePolling.ts`：轮询 hook。
- `web/src/layouts/MainLayout.tsx`：侧边菜单、顶部集群摘要和 SSE 状态。
- `web/src/router.tsx`：HashRouter 路由和登录守卫。

路由大致对应后端业务域：

- `/dashboard`、`/monitor`、`/statistics`
- `/servers`、`/clients`
- `/accounts`、`/access`
- `/sources`、`/aliases`、`/sourcetable`
- `/relay/pull`、`/relay/push`
- `/history`、`/audit`、`/logs/ring`
- `/settings`

注意：`probeBackend()` 目前只调用公开的 `/api/status/health`，并不会真正验证 token 是否有效；它只是确认 baseURL 可达。

## 协议与数据类型

- `.proto` 源文件在 `proto/caster`。
- 生成后的 C++ 文件在 `proto/src`，并被 `proto/CMakeLists.txt` 直接编译进 `caster_proto`。
- API 契约检查命令：`node tools/contract_check/check_api_contracts.mjs`。
- NC-010 后该命令覆盖关键管理台 message 字段、enum 成员名和 enum 数字值与 `web/src/api/types.ts` 的漂移，并已接入主 CI。
- 当前允许差异和维护规则见 `api/api-contract-sync.md`；新增差异必须写明原因，不能静默跳过。
- 如果修改 proto，需要同步重新生成 `proto/src`，检查 HTTP JSON 和前端 `web/src/api/types.ts`，并运行契约检查。
- 生成方式可先参考 `references/proto/build-pb-legacy.txt`。

## 后续改动建议路径

改后端 REST API：

1. `src/http/http_handler.*` 增改路由和 handler。
2. 如涉及 Redis hash/schema，同步 `src/core` 或 `src/auth`。
3. 更新 `web/src/api/index.ts` 和 `web/src/api/types.ts`。
4. 更新页面或 hook。
5. 运行 `node tools/contract_check/check_api_contracts.mjs`。
6. 必要时更新 `api/api-reference.md` 和 `deploy/scripts/e2e_smoke.sh`。

改 NTRIP 连接行为：

1. 先看 `ntrip_listener` 的请求解析和 `ConnectInfo` 字段。
2. 再看对应 session：server/client/near/source/relay。
3. 涉及状态广播时检查 `CASTER::Register_Record`、`Pub_Raw_Data`、`Sub_Raw_Data`。
4. 涉及鉴权时检查 `AUTH::Verify` 与 `Auth_Verify.yml` 解析。

改配置：

1. 修改 `cmake/*.yml.in` 模板。
2. 修改 `src/service/ntrip_config.cpp` 解析。
3. 如果是结构化字段，检查 `proto/caster/service/*.proto` 和 `proto/src/service/*.pb.*`。
4. 确认打包脚本是否需要替换默认值，例如 `Web_Root: "./web"`。

改前端页面：

1. 优先复用 `web/src/api/index.ts` 中已有 API。
2. 实时数据优先用 `useSSE`，详情或低频页面可用 `usePolling`。
3. 表字段要和后端 JSON/proto 字段一致，避免只在 UI 层做别名。

## 已知注意点

- 仓库刚重新 clone，当前 `git status` 干净，但 Windows Git 会反复警告 `C:\Users\KOROyo/.config/git/ignore` permission denied；这不代表仓库脏。
- Windows 环境下 `git submodule status` 可能因为 Git 自带 Unix helper 缺失而失败；当前各 `third_party` 子模块目录已存在。
- 团队任务 worktree 默认从 `repo\third_party` 本地水合子模块，不让每个任务 worktree 单独从远端拉取。
- 顶层构建会把二进制和配置输出到源码树下的 `bin/<BuildType>`。
- `archive/historical/data-flow-and-architecture.md` 对 HTTP event loop 的描述已落后于源码。
- `web/README.md` 还是 Vite 模板说明，不是项目说明。
- 协议、历史需求、早期计划和草稿代码已经迁入 `references/` 与 `archive/`；动手前要优先看源码和 `current/` 下的新文档。

## 2026-06-14 NC-016 活跃账号 Redis/SSE e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeActiveAccounts`。
- 该模式使用 Redis fixture 种入 `STR:ACTIVE`、`ACT:SESSION:*` 和 `ACT:ACTIVE`
  噪声数据，并验证 `/api/accounts/active` 与 SSE `account_actives` 初始快照同源。
- 覆盖：legacy fallback、`ACT:SESSION:*` 优先、多连接同账号、密码材料剥离、
  `ACT:ACTIVE` 不作为在线会话来源。
- PowerShell 写 JSON seed 必须用 `redis-cli -x HSET` 从 stdin 输入值；直接把 JSON
  作为 native 参数传给 `docker exec redis-cli HSET` 会在 Windows 上丢失双引号，
  造成 Redis 中存入非法 JSON。
- 该 e2e 只覆盖读侧真实 Redis/HTTP/SSE；NC-017 已补真实 NTRIP/Auth 登录写入和
  断连清理，NC-018 已补踢线矩阵，NC-019 已补续期长跑；多节点场景仍是后续缺口。

## 2026-06-14 NC-020 Active Account SSE 运行中增量 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeActiveAccountSseDelta`。
- 该模式在服务运行并登录后，打开真实
  `/api/events/stream?channels=account_actives` SSE 连接。
- 在同一条 SSE 连接上验证唯一 `ACT:SESSION:<account>` field 的新增、更新和删除
  都会触发 `account_actives` 事件。
- 每一步都用 `/api/accounts/active` 交叉验证 REST 与 SSE payload 同源，并继续
  验证密码材料剥离。
- 该 e2e 闭合单节点运行中 `account_actives` 增量推送缺口；多节点
  AUTH:BROADCAST 和 relay failover 仍是后续专项；匿名登录矩阵由 NC-021 覆盖。

## 2026-06-14 NC-017 NTRIP Auth Session 写侧 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripAuthSession` 和
  `-NtripPort`。
- 该模式使用 Docker Redis 8.6.3 fixture，临时关闭 rover/client 匿名登录，
  seed `ACT:ACTIVE` 实名账号，建立真实 NTRIP POST source 和 GET client。
- 覆盖：真实 Basic Auth client 登录、`ACT:SESSION:<account>` 写入、REST
  `/api/accounts/active` 读取真实会话、密码材料剥离，以及 client 断连后
  `ACT:SESSION:<account>` 对应 field 被清理。
- NC-021 已覆盖单节点匿名登录允许/拒绝矩阵；NC-017 自身仍不覆盖多节点
  AUTH:BROADCAST 和 relay failover。

## 2026-06-14 NC-018 NTRIP Online_Protection e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripOnlineProtection` 和
  `-NtripOnlineProtectionScenario RejectNew|KickOld`。
- 两种场景必须分别启动服务，因为 `Rover_Setting.Online_Protection` 是启动时配置。
- `RejectNew` 覆盖 `Online_Protection=true`、`connection_limit=1` 时第二个同账号
  client 被拒绝/关闭，旧 client 保留。
- `KickOld` 覆盖 `Online_Protection=false`、`connection_limit=1` 时第二个同账号
  client 成功登录，旧 client 被关闭。
- 两种场景都断言 `ACT:SESSION:<account>`、`ACT:REC:<account>`、`USR:REC:<account>`
  最终字段集合与预期 connect_key 一致，并验证 `/api/accounts/active` 不泄露密码材料。
- NC-021 已覆盖单节点匿名登录允许/拒绝矩阵；NC-018 自身仍不覆盖多节点
  AUTH:BROADCAST 和 relay failover。

## 2026-06-14 NC-019 NTRIP Auth Session 续期 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripAuthSessionRenewal`
  和 `-NtripRenewalWaitSec`。
- 续期 smoke 使用 Redis 8.6.3 Docker fixture、真实 NTRIP POST source 和实名
  GET client，默认等待 25 秒，跨过 Auth 默认 5 秒更新周期和 10 秒 field TTL。
- 覆盖：`ACT:SESSION:<account>` 同 connect_key 在连接存活期间持续存在，
  `update_time` 从初始值单调增长，`online_time` 保持不变。
- 同时通过 `HTTL` 断言 `ACT:SESSION:<account>`、`ACT:REC:<account>`、
  `USR:REC:<account>` 三处 field TTL 为正，并验证 `/api/accounts/active`
  与续期后的真实会话一致且不泄露密码材料。
- client 断连后确认 `ACT:SESSION/ACT:REC/USR:REC` 对应 field 都被清理。
- `check_redis_compat.{ps1,sh}` 同步纳入 `HTTL` 兼容检查，避免 QA 依赖未声明命令。
- NC-021 已覆盖单节点匿名登录允许/拒绝矩阵；NC-019 自身仍不覆盖多节点
  AUTH:BROADCAST 和 relay failover。

## 2026-06-14 NC-021 NTRIP 匿名登录矩阵 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripAnonymousAuth`
  和 `-NtripAnonymousScenario AllowAnonymous|RejectAnonymous`。
- 两种场景必须分别启动服务，因为 `Rover_Setting.Anonymous_Login` 是启动时配置。
- `AllowAnonymous` 覆盖无 Basic Auth rover 成功连接，`ACT:UND:<name>` 写入且
  field `HTTL` 为正，匿名 client 不写入 `ACT:SESSION:*`，也不出现在
  `/api/accounts/active`，断连后 `ACT:UND:<name>` field 被清理。
- `RejectAnonymous` 覆盖无 Basic Auth rover 被拒绝/关闭，且
  `ACT:UND/ACT:SESSION/ACT:REC/USR:REC` 不残留该匿名连接。
- NC-022 已覆盖本地双实例 `AUTH:BROADCAST` 踢旧连接；NC-023 已覆盖禁用/失效
  账号矩阵；NC-021 自身仍不覆盖 relay failover。

## 2026-06-14 NC-022 NTRIP Auth Broadcast 跨实例 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripAuthBroadcast`，
  可用 `-NtripBroadcastHttpPort` 和 `-NtripBroadcastNtripPort` 指定第二实例端口。
- 该 smoke 在一个 Redis 8.6.3 Docker fixture 下启动两个本地 `CasterService`
  进程，第二实例使用临时 conf 目录和 `-conf <dir>\` 启动。
- 测试账号固定 `connection_limit=1`，两实例均设置 `Online_Protection=false`、
  `Rover_Setting.Anonymous_Login=false`、`Update_Intv=1`，并复用真实 NTRIP
  POST source 和实名 GET client。
- 覆盖：Node A 先建立实名 client 并写入 `ACT:SESSION/ACT:REC/USR:REC`；
  Node B 使用同账号建立新 client，触发 KickOld 和 `AUTH:BROADCAST`；Node A
  旧 TCP client 被关闭。
- 覆盖：`ACT:SESSION:<account>`、`ACT:REC:<account>`、`USR:REC:<account>`
  最终只保留 Node B connect_key，Node A/Node B 两边 `/api/accounts/active`
  均只展示 Node B 连接。
- 覆盖：等待至少一个更新周期后再次确认 Node A 旧 connect_key 不会被重写。
- 该 e2e 闭合本地双实例 `AUTH:BROADCAST` 踢线链路；禁用/失效账号矩阵由
  NC-023 覆盖；HTTP/API Redis 断线恢复由 NC-024 覆盖；真实多机器 node identity
  和 relay failover 仍需后续专项。

## 2026-06-14 NC-023 NTRIP 禁用/失效账号矩阵 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeNtripDisabledAccount`，
  默认关闭，必须作为独立服务生命周期运行，因为它会通过 HTTP API 变更账号状态。
- 该 smoke 使用 Redis 8.6.3 Docker fixture、本地 `CasterService`、真实 NTRIP
  POST source 和 GET client；账号创建和状态变化均通过 `/api/accounts`。
- 覆盖：enabled 账号创建后 `ACT:ACTIVE` 存在，真实 Basic Auth client 可登录，
  `/api/accounts/active` 展示该实名会话且不泄露密码材料，断连后三处在线 field 清理。
- 覆盖：frozen、inactive 和 expired 三个场景更新后 `ACT:ACTIVE` 被删除，
  同账号真实 NTRIP client 被拒绝并关闭。
- 覆盖：三个拒绝场景均不残留 `ACT:SESSION:<account>`、`ACT:REC:<account>`、
  `USR:REC:<account>` 或 `/api/accounts/active` 记录。
- 该 e2e 闭合禁用/失效账号矩阵；HTTP/API Redis 断线恢复由 NC-024 覆盖；
  真实多机器 node identity 和 relay failover 仍需后续专项。

## 2026-06-14 NC-024 Redis 断线/重连 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeRedisReconnect`，仅支持
  Docker Redis fixture，因为测试需要停止并重启同一 Redis 容器。
- 首轮运行暴露真实缺口：Redis 容器恢复后，HTTP API async `redis_adapter`
  没有重新建立连接，`/api/status` 长时间保持 `redis_caster_connected=false`
  和 `redis_auth_connected=false`。
- `src/http/redis_adapter.*` 新增 HTTP event loop 上的重连 timer。断开时安排
  约 1/4/6/8/10 秒退避重连，重连成功后恢复 `_connected=true` 并执行 pending command；
  析构时取消 timer 并避免关闭路径误触发重连。
- 覆盖：Redis fixture 停止后 `CasterService` 进程保持存活，`/api/status/health`
  仍返回 ok。
- 覆盖：同一 Redis fixture 重启后，`/api/status` 中 caster/auth Redis 连接恢复，
  随后重新登录并查询 `/api/status`、`/api/monitor/cluster` 成功。
- 该 e2e 闭合 HTTP/API 层 Redis 短断重连缺口；relay failover 和真实多机器
  node identity 仍需后续专项。

## 2026-06-14 NC-025 本地双实例 Node Identity / Cluster e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeLocalDualNodeIdentity`，
  可用 `-NtripBroadcastHttpPort` 和 `-NtripBroadcastNtripPort` 指定第二实例端口。
- 该 smoke 在一个 Redis 8.6.3 Docker fixture 下启动两个本地 `CasterService`
  进程，第二实例使用临时 conf 目录和 `-conf <dir>\` 启动。
- 覆盖：Node A 和 Node B 均可 health/login/status，两个 `/api/status.node_id`
  均匹配 `Node_XXXXX` 且彼此不同。
- 覆盖：等待 core heartbeat 后，Node A 和 Node B 的 `/api/monitor/cluster`
  都能看到两个 online node。
- 覆盖：cluster node payload 中 `listen_port`、`http_port`、`process_id`、
  `hostname` 和 `http_enabled` 与两个本地实例匹配。
- 该 e2e 闭合本地双实例 node identity / cluster monitor 自动化缺口；真实跨主机
  网络分区、反向代理/sticky session 和 relay start/stop failover 仍需后续专项。

## 2026-06-14 NC-026 Relay Pull start/stop 本地真实 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeRelayPullStartStop`，可用
  `-NtripBroadcastHttpPort` 和 `-NtripBroadcastNtripPort` 指定第二实例端口。
- 该 smoke 在一个 Redis 8.6.3 Docker fixture 下启动两个本地 `CasterService`
  进程，第二实例使用临时 conf 目录和 `-conf <dir>\`，并承载真实 NTRIP
  source mount。
- 覆盖：主实例通过 `/api/relays/pull` 创建 enabled pull record 后，真实
  `relay_pull` 连接第二实例目标 mount，`/api/relays/pull/status` 或
  `PULL:STAT` 收敛到 `state=1`、`connect_key` 非空、`node_uid` 等于主实例
  node_id。
- 覆盖：第二实例关闭 rover 匿名登录，relay target 账号必须进入
  `ACT:SESSION/ACT:REC/USR:REC`，且不产生 `ACT:UND` 匿名 rover 记录。
- 覆盖：`/api/monitor/cluster` 中主实例 `pull` 计数相比基线增加，并在
  stop/cleanup 后回落到基线。
- 覆盖：`/api/relays/pull/stop/<uid>` 设置 `enabled=false` 后状态不再
  running，且 not-running 判定同时检查 HTTP status 和 Redis `PULL:STAT`；
  `/api/relays/pull/start/<uid>` 设置 `enabled=true` 后状态重新 running。
- post-merge 验证曾暴露真实产品问题：stop 后连接已停，但旧的本节点
  `PULL:STAT state=1` 快照会被周期同步/上报重新写回，导致 pull count
  不回落。修复后 relay INACTIVE 按 uid 删除状态，周期上报只续期本节点
  真实连接仍存在的 relay 状态，同步回调拒绝恢复本节点无真实连接的
  running stale 状态。
- team-dev @ d7e9a6a 已重新验证 `CasterService`、`schema_smoke`、contract、
  默认 Docker e2e 和 `-IncludeRelayPullStartStop`。
- 该 e2e 闭合本地真实 pull relay 控制链路；push relay、真实跨主机网络分区、
  反向代理/sticky session 和 master lease failover 仍需后续专项。

## 2026-06-14 NC-027 Relay Push start/stop 本地真实 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeRelayPushStartStop`，可用
  `-NtripBroadcastHttpPort` 和 `-NtripBroadcastNtripPort` 指定第二实例端口。
- 该 smoke 在一个 Redis 8.6.3 Docker fixture 下启动两个本地 `CasterService`
  进程，第二实例使用临时 conf 目录和 `-conf <dir>\`，主实例承载真实 NTRIP
  source mount，并推送到第二实例的独立 target mount。
- 覆盖：主实例通过 `/api/relays/push` 创建 enabled push record 后，真实
  `relay_push` 连接第二实例 target mount，`/api/relays/push/status` 或
  `PUSH:STAT` 收敛到 `state=1`、`connect_key` 非空、`node_uid` 等于主实例
  node_id。
- 覆盖：第二实例关闭 source 匿名登录，独立 target mount 出现在 `MPT:LIST`、
  `MPT:REC:<target_mount>` 和 `MPT:STAT`，证明目标实例接受了 relay push source。
- 覆盖：`/api/monitor/cluster` 中主实例 `push` 计数相比基线增加，并在
  stop/cleanup 后回落到基线。
- 覆盖：`/api/relays/push/stop/<uid>` 设置 `enabled=false` 后状态不再
  running，且目标实例 source 被清理；`/api/relays/push/start/<uid>` 设置
  `enabled=true` 后状态重新 running，目标实例 source 重新在线。
- `PUSH:STAT.connect_key` 属于发起侧 relay push 连接，第二实例
  `MPT:REC:<target_mount>` 中的 target source connect_key 由目标实例本地生成；
  脚本断言 target source connect_key 不等于主实例原始 source connect_key，也不等于
  `PUSH:STAT.connect_key`。
- 该 e2e 闭合本地真实 push relay 控制链路；数据内容完整性、真实跨主机网络分区、
  反向代理/sticky session 和 master lease failover 仍需后续专项。

## 2026-06-14 NC-028 Relay 数据转发本地真实 e2e

- Windows `deploy/scripts/e2e_smoke.ps1` 新增 `-IncludeRelayDataForwarding`，默认
  关闭，复用 NC-026/NC-027 的本地双实例 relay start/stop fixture。
- 该 smoke 在一个 Redis 8.6.3 Docker fixture 下顺序运行 pull 和 push 数据路径，
  每个路径均启动第二个本地 `CasterService` 实例、使用临时 conf 目录和独立
  HTTP/NTRIP 端口。
- Pull 数据路径：Node B 提供真实 NTRIP source mount，Node A 通过
  `/api/relays/pull` 创建 pull relay；Node A 下游 rover client 订阅本地 relay
  mount 后，Node B source socket 写入 `NC028-PULL-DATA` payload，Node A client
  收到完整 payload。
- Push 数据路径：Node A 提供真实 NTRIP source mount，Node A 通过
  `/api/relays/push` 创建 push relay 到 Node B 独立 target mount；Node B 下游
  rover client 订阅 target mount 后，Node A source socket 写入 `NC028-PUSH-DATA`
  payload，Node B client 收到完整 payload。
- 数据断言不替代既有控制面断言：pull/push start/stop/status、cluster count、
  target mount/source lifecycle 和 cleanup 仍沿用 NC-026/NC-027 路径。
- 该 e2e 补齐本地真实 relay 数据内容转发证据；长时间大吞吐、丢包/乱序、真实跨主机
  网络分区、反向代理/sticky session 和 master lease failover 仍需后续专项。

## 2026-06-16 NC-034 Ninja 全核并行构建默认化

- C++ 本地构建默认入口已收束为 CMake + Ninja + 全处理器并行。
- `CMakePresets.json` 提供 `ninja-release` 和 `ninja-debug` preset。
- Windows 使用 `deploy/scripts/build_ninja.ps1`，Linux 使用 `deploy/scripts/build_ninja.sh`。
- Windows 脚本会自动加载 Visual Studio 2022 x64 developer environment，并优先选择可执行的 Ninja。
- Windows 基线配置入口使用
  `.\deploy\scripts\build_ninja.ps1 -BuildType Release -ConfigureOnly`。普通 PowerShell
  下的裸 `cmake --preset ninja-release` 只适用于已准备好真实 Ninja 和 C/C++ compiler
  PATH 的环境，不能单独作为团队准入命令。
- CI 和 package 脚本显式使用 `-G Ninja`，并显式传入处理器并行数。
- NC-034 已验证 `schema_smoke` Ninja 构建、`schema_smoke.exe`、CTest `schema_smoke` 和 `CasterService` Ninja 构建。
- 后续 C++ 任务卡、QA 和 Review 默认使用 Ninja 构建入口；若任务必须使用其他 generator，需要在任务卡或 QA 记录中说明原因。
