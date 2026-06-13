# NavCaster 项目记忆

生成时间：2026-06-12
基线复核：2026-06-13

当前团队基线：`team-dev`，以 Git 最新提交和 `doc/iteration-progress.md` 最近记录为准。

说明：本文是当前项目入口记忆。若和源码、`doc/workflow.md` 或
`doc/iteration-progress.md` 冲突，以源码和最近迭代记录为准。

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
- `doc`：较新的架构/API/计划文档。
- `docs`：协议、早期需求和参考资料，部分内容偏历史。
- `third_party`：子模块形式的 abseil、protobuf、hiredis、libevent、yaml-cpp、rtklib、spdlog、json。

## 构建与打包

后端构建：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Windows CI 打包：

```powershell
powershell -ExecutionPolicy Bypass -File deploy\ci\build_in_windows.ps1
```

Linux CI 打包：

```bash
BUILD_TYPE=Release PACKAGE_NAME=NavCaster-local bash deploy/ci/build_in_linux.sh
```

前端：

```bash
cd web
npm ci
npm run dev
npm run build
```

打包脚本要求 `web/dist/index.html` 已存在，然后把二进制、`conf/`、`web/`、`scripts/` 复制到 `release/<PackageName>`。Linux 打包脚本还会拉取并构建 Redis。

Docker runtime：

```bash
docker compose -f deploy/docker/docker-compose.yml up -d
```

默认使用 `release/NavCaster-local` 作为容器 package，可通过 `NAVCASTER_PACKAGE` 覆盖。

Smoke test：

```bash
BASE=http://127.0.0.1:8080 USER=admin PASS=admin bash deploy/scripts/e2e_smoke.sh
```

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

这一点和 `doc/data-flow-and-architecture.md` 中“单 event_base”的描述不一致，以源码为准。

HTTP 启动策略：

- 默认只有 Redis 选出的主节点开启 HTTP API。
- `HTTP_API_Setting.Force_Enable` 如果在配置里设置为 true，则绕过主节点判断直接开启。
- 当前配置模板显式写出 `Force_Enable: false`。部署契约见 `doc/http-deployment.md`。
- 当前 HTTP listener 一旦启动不会因后续 Master 丢失自动关闭。

Redis 部署策略：

- 生产最低版本为 Redis Open Source 8.4.0+。
- 当前 Docker、CI 和 Linux package 默认 Redis 版本为 8.6.3。
- 关键命令依赖：`HSETEX`、`HEXPIRE`、`SET ... IFEQ ... EX`。
- 部署前用 `deploy/scripts/check_redis_compat.sh` 或 `.ps1` 校验外部 Redis。

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
`ACT:SESSION:*` JSON 契约，以及活跃账号 REST/SSE 读侧的新旧源兼容。

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
- 当前允许差异和维护规则见 `doc/api-contract-sync.md`；新增差异必须写明原因，不能静默跳过。
- 如果修改 proto，需要同步重新生成 `proto/src`，检查 HTTP JSON 和前端 `web/src/api/types.ts`，并运行契约检查。
- 生成方式可先参考 `docs/build-pb.txt`。

## 后续改动建议路径

改后端 REST API：

1. `src/http/http_handler.*` 增改路由和 handler。
2. 如涉及 Redis hash/schema，同步 `src/core` 或 `src/auth`。
3. 更新 `web/src/api/index.ts` 和 `web/src/api/types.ts`。
4. 更新页面或 hook。
5. 运行 `node tools/contract_check/check_api_contracts.mjs`。
6. 必要时更新 `doc/api-reference.md` 和 `deploy/scripts/e2e_smoke.sh`。

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
- `doc/data-flow-and-architecture.md` 对 HTTP event loop 的描述已落后于源码。
- `web/README.md` 还是 Vite 模板说明，不是项目说明。
- `docs` 目录里有协议和历史资料，动手前要优先看源码和 `doc` 下的新文档。
