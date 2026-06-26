# NavCaster QA Gates

更新时间：2026-06-16

本文档定义 `team-dev` 之后的最低验证门槛。团队级流程见
`F:\Projects\NavCaster\_team\QUALITY_GATES.md`。

## CI 硬门槛

主 CI `.github/workflows/build-and-package.yml` 当前只调用统一打包入口：

```text
Windows:
  deploy/scripts/package_windows.ps1

Linux:
  Docker ubuntu-20.04 container
  -> deploy/scripts/package_linux.sh
```

两个打包入口都会先检查并尽量补全构建环境，然后执行 API contract check、Web build、
Ninja Release 构建、`schema_smoke` CTest、发布目录组装和归档。产物统一输出到
`dist/<PackageName>/`，并生成 `dist/<PackageName>.zip` 或
`dist/<PackageName>.tar.gz`。不传参数时打包脚本必须使用默认 Release、默认 `dist/`
输出、默认版本解析和默认平台解析；默认版本优先使用最新 Git tag 加距 tag 提交数，
例如 `2.0.1-189`，生成的目录名和归档名必须带版本号。脚本同时写出
`dist/package-metadata.env`，GitHub Actions 只根据该元数据上传 `dist` 中对应归档。

API contract check 会比较 `proto/caster` 中关键 message 字段、enum 成员名和
enum 数字值与 `web/src/api/types.ts` 的同步状态。未知差异会失败；当前阶段性允许差异
记录在 `api/api-contract-sync.md` 和脚本 allowlist 中。

`npm run lint` 是前端任务的目标门槛，但当前代码基线仍有既有 ESLint
错误；在修复该债务前不作为主 CI 硬门槛。前端任务仍必须运行并记录 lint
结果，不能用 build 通过替代 lint 结果。

`schema_smoke` 已注册为 CTest 测试，名称为：

```text
schema_smoke
```

## 本地常用命令

Windows：

```powershell
.\deploy\scripts\admission_check.ps1 -BuildType Release
```

Windows 普通 PowerShell 下，基线配置入口必须使用
`.\deploy\scripts\build_ninja.ps1 -BuildType Release -ConfigureOnly`。该脚本会自动
加载 Visual Studio x64 developer environment，并在 PATH 上的 WinGet `ninja.exe`
shim 不可执行时退回 Visual Studio 自带 Ninja。裸 `cmake --preset ninja-release`
只适用于当前 shell 已经能直接解析真实 `ninja`、C/C++ compiler 的开发者环境。

`admission_check.ps1` 的强阻断项为：

```text
API contract check
Ninja configure
schema_smoke Ninja build
schema_smoke executable
CTest schema_smoke
Web npm ci
Web production build
```

`npm run lint` 暂为报告项，可用 `-IncludeLint` 手动纳入输出；在现有前端 lint
基线债修复前，不升级为全仓库硬阻断。运行态 e2e matrix 依赖 Docker/Redis/端口/
多进程生命周期，默认按任务风险触发并在 QA 记录中列明命令、结果和环境缺口。

Linux：

```bash
node tools/contract_check/check_api_contracts.mjs
BUILD_TYPE=Release bash deploy/scripts/build_ninja.sh --target schema_smoke
ctest --test-dir build/ninja-Release --output-on-failure -R schema_smoke

cd web
npm ci
npm run lint
npm run build
```

## 手动或环境依赖门槛

### Linux runtime image provenance

Linux runtime image 任务必须从当前 commit 构建不可变 tag，并记录 image id、
`org.opencontainers.image.revision` label 和对应 `team-dev` commit；不得使用
`navcaster:latest` 作为 QA 证据。

```bash
bash deploy/scripts/build_runtime_image.sh
docker image inspect navcaster:team-dev-<short12> \
  --format 'image={{.Id}} revision={{index .Config.Labels "org.opencontainers.image.revision"}} version={{index .Config.Labels "org.opencontainers.image.version"}}'
```

Docker bridge cluster 与 HTTP ingress strategy smoke 必须显式传入当前 commit image：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RootPath . -IncludeDockerBridgeCluster -NavCasterImage navcaster:team-dev-<short12>
.\deploy\scripts\e2e_smoke.ps1 -RootPath . -IncludeHttpIngressStrategy -NavCasterImage navcaster:team-dev-<short12>
```

若 Docker Engine、Linux builder 或镜像拉取不可用，QA 记录必须标为
`QA_BLOCKED` 或列明降级证据，不得用旧 `navcaster:latest` 补位。

运维故障处理和证据模板见 `deployment/ops-runbook.md`。该入口把 master lease、
cluster 节点视图、HTTP fixed/sticky 入口、relay failover、Redis TTL/key 监控、
runtime image provenance 和运行态 smoke matrix 按值班场景串联。

## 文档治理任务门槛

纯文档治理任务不得新增运行态 e2e 场景，也不得把产品源码逻辑改动混入同一 diff。
最低检查项：

```powershell
git status --short
git diff --check
git diff --name-status team-dev...HEAD
git diff --stat team-dev...HEAD
rg --files doc
Test-Path docs
rg -n "repo\\docs|repo/docs|docs\\|navcaster:latest" doc F:\Projects\NavCaster\_team F:\Projects\NavCaster\shared
```

预期：

- `docs` 不再作为仓库根目录下的并列文档入口存在。
- 当前事实文档不得引用迁移前的 `doc/*.md` 旧路径或 `repo\docs` 入口。
- 历史计划、旧需求、协议参考、草稿代码必须位于 `references/` 或 `archive/`，并标注非当前实现事实。
- `git diff --name-status` 只能出现文档、参考资料或归档素材路径；不得包含 `src/`、`web/`、`proto/`、`deploy/` 等产品源码或运行脚本逻辑改动。

Windows HTTP e2e smoke 优先使用脚本自动编排 Redis fixture、临时配置
`CasterService` 并清理现场：

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target CasterService
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release
```

默认 Docker fixture 使用 `redis:8.6.3`，映射到 `127.0.0.1:16379`，并复用
`deploy\scripts\check_redis_compat.ps1` 校验 `HSETEX`、`HEXPIRE`、`HTTL`
和 `SET ... IFEQ ... EX`。脚本会临时把 `bin\<config>\conf\Service_Setting.yml`
的 HTTP API 改为 `Force_Enable: true`，把 `Caster_Core.yml` 与
`Auth_Verify.yml` 指向 fixture Redis；结束后恢复配置、停止服务并删除容器。
如果 `16379` 已被本机 Redis 或其他服务占用，使用 `-RedisPort 16380` 等空闲端口。
如果默认 NTRIP 端口 `4202` 被占用，使用 `-NtripPort 14202` 等空闲端口。

活跃账号 REST/SSE 读侧深度 smoke 使用同一个 Windows fixture 入口：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeActiveAccounts
```

该模式会在 fixture Redis 中写入唯一前缀的 `STR:ACTIVE` legacy 数据、
`ACT:SESSION:*` 新会话数据和 `ACT:ACTIVE` 登录索引噪声数据，然后验证
`GET /api/accounts/active` 与 SSE `account_actives` 初始快照同源。覆盖点包括：
`ACT:SESSION:*` 优先覆盖 legacy 同 field、仅 legacy fallback、仅新会话、多连接同账号、
输出剥离密码材料，以及 `ACT:ACTIVE` 不被当作在线会话来源。脚本写入 JSON seed 时
使用 `redis-cli -x HSET` 从 stdin 传值，避免 Windows/Docker native 参数层破坏 JSON
双引号。成功和失败路径都必须清理 seed、恢复配置、停止服务并删除 fixture 容器。

活跃账号运行中 SSE 增量 smoke 使用同一个 Windows fixture 入口：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeActiveAccountSseDelta
```

该模式会在服务启动并登录后打开真实
`/api/events/stream?channels=account_actives` SSE 连接，然后在 Redis 中对唯一
`ACT:SESSION:<account>` field 依次执行新增、更新和删除。每一步必须收到同一条
SSE 连接上的 `account_actives` 事件，并用 `/api/accounts/active` 交叉验证
payload 同源；REST/SSE 都必须剥离密码材料。成功和失败路径都必须关闭 SSE
连接并清理 Redis seed、服务进程、配置和 fixture 容器。

NTRIP/Auth 写侧 active session 深度 smoke 使用真实 NTRIP TCP source/client 连接：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAuthSession
```

该模式会临时把 `Rover_Setting.Anonymous_Login` 设为 `false`，保持
`Base_Setting.Anonymous_Login` 为 `true`，在 `ACT:ACTIVE` 中写入唯一实名 rover
账号 fixture，然后：

```text
1. 打开真实 NTRIP POST source 连接，形成 live mountpoint。
2. 打开真实 NTRIP GET client 连接并使用 Basic Auth 登录。
3. 轮询 Redis，确认 Auth/Core 写入 ACT:SESSION:<account>。
4. 验证 /api/accounts/active 能读取该真实会话且不泄露密码材料。
5. 关闭 client socket，确认 ACT:SESSION:<account> 对应 connect_key 被 HDEL 清理。
```

该检查覆盖真实 NTRIP listener、Auth 验证、`AUTH::Add_Login_Record`、
Core register/subscribe 和 active account REST 读侧的串联路径。它不覆盖
`Online_Protection` 踢线矩阵或长时间续期；跨实例 `AUTH:BROADCAST` 由
`-IncludeNtripAuthBroadcast` 专项覆盖。

NTRIP/Auth active session 续期长跑 smoke 使用同一个真实 NTRIP 入口：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAuthSessionRenewal
```

该模式默认等待 25 秒，跨过 Auth 默认 5 秒更新周期和 10 秒 field TTL，验证：

```text
1. 真实 NTRIP POST source 与实名 GET client 建立。
2. ACT:SESSION:<account> 初始 field 出现并记录 update_time。
3. 同 field 在续期窗口后仍存在，value.update_time 单调增长。
4. ACT:SESSION:<account>、ACT:REC:<account>、USR:REC:<account> 对应 field 的 HTTL 均为正。
5. /api/accounts/active 与续期后的真实会话一致且不泄露密码材料。
6. client 断连后 ACT:SESSION/ACT:REC/USR:REC 对应 field 被清理。
```

如需调试可用 `-NtripRenewalWaitSec 25` 显式指定等待秒数；常规回归不要低于
Auth/Core 默认续期间隔与 TTL 组合，否则不能证明续期链路真实工作。

NTRIP/Auth `Online_Protection` 连接数矩阵深度 smoke 需要分场景运行，因为
`Online_Protection` 是服务启动时读取的配置：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripOnlineProtection -NtripOnlineProtectionScenario RejectNew
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripOnlineProtection -NtripOnlineProtectionScenario KickOld
```

两个场景都会使用真实 NTRIP POST source 和两个同账号 GET client，并临时固定
`Caster_Setting.Update_Intv=1`、`Rover_Setting.Enable_Mult=true`、`Keep_Early=false`。
`RejectNew` 断言 `Online_Protection=true` 且 `connection_limit=1` 时第二个
client 被拒绝/关闭，`ACT:SESSION:<account>`、`ACT:REC:<account>` 与
`USR:REC:<account>` 最终只保留第一个 `connect_key`。`KickOld` 断言
`Online_Protection=false` 时第二个 client 登录成功、旧 socket 被关闭，上述三个
Redis hash 最终只保留第二个 `connect_key`。两个场景都继续验证
`/api/accounts/active` 与真实在线会话一致且不泄露密码材料。

NTRIP/Auth 匿名登录矩阵 deep smoke 需要分场景运行，因为
`Rover_Setting.Anonymous_Login` 是服务启动时读取的配置：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAnonymousAuth -NtripAnonymousScenario AllowAnonymous
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAnonymousAuth -NtripAnonymousScenario RejectAnonymous
```

两个场景都会使用真实 NTRIP POST source 和无 Basic Auth 的 GET client。
`AllowAnonymous` 临时设置 `Rover_Setting.Anonymous_Login=true`，断言 client
成功连接，`ACT:UND:<name>` 写入且 field `HTTL` 为正，同时确认
`ACT:SESSION:*` 和 `/api/accounts/active` 不包含匿名 client，断连后
`ACT:UND:<name>` field 被清理。`RejectAnonymous` 临时设置
`Rover_Setting.Anonymous_Login=false`，断言无 Basic Auth client 被拒绝/关闭，
且 `ACT:UND`、`ACT:SESSION`、`ACT:REC`、`USR:REC` 不残留该匿名连接。

NTRIP/Auth Broadcast 跨实例 deep smoke 使用同一个 Redis fixture 拉起两个本地
`CasterService` 进程：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAuthBroadcast -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式会为第二实例复制临时 conf 目录，并通过 `-conf <dir>\` 指向独立配置。
测试固定 `connection_limit=1` 和 `Online_Protection=false`，先让 Node A 建立
实名 rover client，再让 Node B 同账号新 client 登录。必须验证：

```text
1. Node A 真实 client 先写入 ACT:SESSION/ACT:REC/USR:REC。
2. Node B 真实 client 登录后触发 KickOld/AUTH:BROADCAST。
3. Node A 旧 TCP client 被关闭。
4. ACT:SESSION:<account>、ACT:REC:<account>、USR:REC:<account> 最终只保留 Node B connect_key。
5. Node A 和 Node B 的 /api/accounts/active 均只展示 Node B 连接。
6. 等待至少一个更新周期后 Node A 旧 connect_key 不会被重写。
```

该检查覆盖本地双进程 `AUTH:BROADCAST` 关闭旧会话链路；HTTP/API Redis 断线恢复
由 NC-024 覆盖，本地双实例 node identity/cluster 由 NC-025 覆盖；真实跨主机
网络分区和 relay failover 仍需专项任务覆盖。

本地双实例 Node Identity / Cluster deep smoke 使用同一个 Redis fixture 拉起两个
本地 `CasterService` 进程：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeLocalDualNodeIdentity -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式会为第二实例复制临时 conf 目录，并通过 `-conf <dir>\` 指向独立配置。
两个实例都使用 `Force_Enable=true` 和短 heartbeat 周期。必须验证：

```text
1. Node A 和 Node B 均可 health/login/status。
2. Node A 与 Node B 的 /api/status.node_id 均匹配 Node_XXXXX。
3. 两个 node_id 不同。
4. 等待 heartbeat 后，两个 HTTP 入口的 /api/monitor/cluster 均包含两个 online node。
5. cluster node payload 的 listen_port/http_port/process_id/hostname/http_enabled 与对应实例匹配。
6. finally 清理第二实例进程、临时 conf、主服务配置和 Docker fixture。
```

该检查覆盖同主机多实例通过 `hostname:listen_port:http_port` 派生不同 node_id，
以及 cluster monitor 双 HTTP 入口读侧一致性；真实跨主机网络分区、反向代理/sticky
session 和 relay failover 仍需专项任务覆盖。

Relay Pull start/stop deep smoke 使用同一个 Redis fixture 拉起两个本地
`CasterService` 进程，并让第二实例提供真实 NTRIP source mount：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeRelayPullStartStop -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式会为第二实例复制临时 conf 目录，并通过 `-conf <dir>\` 指向独立配置。
测试必须验证：

```text
1. Node A 和 Node B 均可 health/login/status，且 node_id 不同。
2. Node B 真实 NTRIP source mount 成功上线。
3. Node A 通过 /api/relays/pull 创建 pull record 成功。
4. /api/relays/pull/status 或 Redis PULL:STAT 对应 uid 收敛到 state=1、connect_key 非空、node_uid=Node A node_id。
5. Node B 关闭 rover 匿名登录，relay target 账号写入 ACT:SESSION/ACT:REC/USR:REC，且不产生 ACT:UND 匿名 rover 记录。
6. /api/monitor/cluster 中 Node A pull count 相比基线至少增加 1。
7. POST /api/relays/pull/stop/<uid> 后 record.enabled=false，HTTP status 与 Redis PULL:STAT 均不再 running，Node A pull count 回落到基线。
8. POST /api/relays/pull/start/<uid> 后 record.enabled=true，PULL:STAT 再次 state=1。
9. finally 清理 pull record/status、NTRIP source、第二实例进程、临时 conf、主服务配置和 Docker fixture。
```

该检查覆盖本地真实 pull relay 控制链路：HTTP relay API、RelayScheduler 广播、
`relay_pull` NTRIP I/O、`PULL:STAT` 回写和 cluster pull count。它不覆盖 push relay、
真实跨主机网络分区、反向代理/sticky session 或 master lease failover。

Relay stop 回归不能只看 stop API 返回或本地日志中的 `relay_pull stopped`。如果
本节点旧 `PULL:STAT state=1` 快照被同步回本地并再次续期，cluster pull count 会
继续显示 running；因此 stop/cleanup 必须同时断言 HTTP status、Redis `PULL:STAT`
和 cluster pull count 均收敛。

Relay Push start/stop deep smoke 使用同一个 Redis fixture 拉起两个本地
`CasterService` 进程，并让主实例提供真实 NTRIP source mount，再推送到
第二实例的独立 target mount：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeRelayPushStartStop -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式会为第二实例复制临时 conf 目录，并通过 `-conf <dir>\` 指向独立配置。
第二实例会关闭 source 匿名登录，测试必须验证：

```text
1. Node A 和 Node B 均可 health/login/status，且 node_id 不同。
2. Node A 的真实 source mount 已在线。
3. Node A 通过 /api/relays/push 创建 enabled push record 成功。
4. /api/relays/push/status 或 PUSH:STAT 收敛到 state=1、connect_key 非空、node_uid 等于 Node A。
5. Node B 独立 target mount 出现在 MPT:LIST、MPT:REC:<target_mount>、MPT:STAT。
6. Node A /api/monitor/cluster 中 push 计数相比基线增加。
7. /api/relays/push/stop/<uid> 后 enabled=false，HTTP/Redis 均不再 running，Node B target source 被清理，push 计数回到基线。
8. /api/relays/push/start/<uid> 后 enabled=true，PUSH:STAT 重新 running，Node B target source 重新在线。
9. cleanup 后 push record/status、target source、source socket、临时 conf 和 fixture 容器均被清理。
```

`PUSH:STAT.connect_key` 属于发起侧 relay push 连接，Node B
`MPT:REC:<target_mount>` 中的 target source connect_key 由目标实例本地生成；
脚本必须断言 target source connect_key 既不同于 Node A 原始 source connect_key，
也不同于 `PUSH:STAT.connect_key`。该检查覆盖本地真实 push relay 控制链路，不覆盖
数据内容完整性、真实跨主机网络分区、反向代理或 master lease failover。

Relay data forwarding deep smoke 复用 pull/push 本地双实例夹具，并在 relay
running 后写入确定性 payload，验证下游 client 读取到完整字节内容：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeRelayDataForwarding -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式会顺序执行 pull 数据路径和 push 数据路径，不能与其他本地双实例 smoke
并行使用同一个 Redis fixture 端口。测试必须验证：

```text
1. Pull 场景中 Node B 提供真实 source mount，Node A 创建 pull relay。
2. Node A 下游 rover client 订阅 pull relay 的本地 mount。
3. Node B source socket 写入 NC028-PULL-DATA payload 后，Node A client 收到完整 payload。
4. Push 场景中 Node A 提供真实 source mount，Node A 创建 push relay 到 Node B 独立 target mount。
5. Node B 下游 rover client 订阅独立 target mount。
6. Node A source socket 写入 NC028-PUSH-DATA payload 后，Node B client 收到完整 payload。
7. 数据断言不削弱既有 stop/start/status 断言，pull/push record、status、source/client socket、第二实例、临时 conf 和 Docker fixture 均被清理。
```

该检查补齐 relay 数据内容转发的本地真实证据；仍不覆盖长时间大吞吐、丢包/乱序、
真实跨主机网络分区、反向代理/sticky session 或 master lease failover。

Relay push failover deep smoke 使用本地双实例夹具，在当前 master/executor 上创建
push relay，停止该节点后验证 survivor 接管并恢复数据转发：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeRelayPushFailover -NtripBroadcastHttpPort 8081 -NtripBroadcastNtripPort 4203
```

该模式必须单独运行，因为它会停止当前 relay executor/master 节点。测试必须验证：

```text
1. 两个本地节点均可 health/login/status/cluster，且 node_id 不同。
2. 当前 master/executor 上创建 enabled push relay，PUSH:STAT state=1、connect_key 非空、node_uid 等于当前 master。
3. survivor 节点 target mount 出现在 MPT:LIST、MPT:REC:<target_mount>、MPT:STAT，且 target source connect_key 不等于原始 source 或 PUSH:STAT connect_key。
4. failover 前 Node A/Node B 路径可转发 NC039-PUSH-FAILOVER-BEFORE payload。
5. 停止当前 master/executor 后，Redis CASTER:MASTER、/api/status.master_node 和 /api/monitor/cluster.master_node 收敛到 survivor。
6. retired node_uid 不再作为 PUSH:STAT running 证据。
7. survivor 上同一 push relay 重新 state=1，node_uid 等于 survivor，connect_key 不复用旧值，cluster push count 增加。
8. failover 后重新打开 source/client，能够转发 NC039-PUSH-FAILOVER-AFTER payload。
9. cleanup 后 push record/status、target source、source/client socket、第二实例、临时 conf 和 Docker fixture 均被清理。
```

该检查补齐 push relay failover 和 failover 后下游 client 恢复收包的本地真实证据。
真实跨物理主机网络分区、丢包/乱序和长时间大吞吐仍由后续专项覆盖。

NTRIP/Auth 禁用/失效账号矩阵 deep smoke 使用 HTTP 账号 API 驱动真实状态变化：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripDisabledAccount
```

该模式必须单独运行，不能与其他实名 NTRIP Include* 混在同一服务生命周期。
测试先创建 enabled 账号并建立真实 NTRIP POST source / GET client，再依次通过
`/api/accounts` 更新同一账号为 frozen、inactive 和 expired。必须验证：

```text
1. enabled 账号创建后 ACT:ACTIVE 存在。
2. enabled 账号真实 Basic Auth client 可登录，ACT:SESSION/ACT:REC/USR:REC 与 /api/accounts/active 一致。
3. enabled client 断连后三处在线 field 清理。
4. frozen 更新后 ACT:ACTIVE 删除，NTRIP client 被拒绝并关闭。
5. inactive 更新后 ACT:ACTIVE 删除，NTRIP client 被拒绝并关闭。
6. expired 更新后 ACT:ACTIVE 删除，NTRIP client 被拒绝并关闭。
7. 三个拒绝场景均不残留 ACT:SESSION:<account>、ACT:REC:<account>、USR:REC:<account> 或 /api/accounts/active 记录。
```

该检查覆盖账号状态从 HTTP 写侧到 Auth 登录索引、真实 NTRIP 拒绝行为和
active account 读侧清理的闭环；HTTP/API Redis 断线恢复由 NC-024 覆盖，
本地双实例 node identity/cluster 由 NC-025 覆盖；真实跨主机网络分区和
relay failover 仍需专项任务覆盖。

HTTP/API Redis 断线/重连 deep smoke 使用 Docker Redis fixture 控制短断窗口：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeRedisReconnect
```

该模式只支持 `RedisMode Docker`。必须验证：

```text
1. 初始 Redis fixture 下 health/login/status/cluster 通过。
2. 停止 Redis fixture 后 CasterService 进程仍存活。
3. Redis 不可用窗口 /api/status/health 仍返回 ok。
4. 重启同一 Redis fixture 后 /api/status 报告 redis_caster_connected=true 且 redis_auth_connected=true。
5. 重连后重新登录，/api/status 和 /api/monitor/cluster 仍可访问。
6. finally 清理服务进程、配置和 Docker fixture。
```

该检查覆盖 HTTP API async Redis adapter 与 blocking Redis client 在短断后的
恢复路径；本地双实例 node identity/cluster 由 NC-025 覆盖；真实跨主机
网络分区和 relay failover 仍需专项任务覆盖。

如果测试机已有外部 Redis 8.4+，可跳过 Docker fixture：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode External -RedisHost 127.0.0.1 -RedisPort 6379
```

Docker engine、`redis-cli` 或目标 Redis 不可用时，脚本必须非零失败并在 QA
记录中说明环境缺口，不允许记为通过。

Linux/macOS 或已有服务进程的 HTTP e2e smoke 可继续使用 Bash 脚本；它需要一个
正在运行的 `CasterService`、可用 Redis、`curl` 和 `jq`：

```bash
BASE=http://127.0.0.1:8080 USER=admin PASS=admin bash deploy/scripts/e2e_smoke.sh
```

HTTP e2e 是 HTTP/API、部署和运行契约任务的最低验证项，但当前不作为主 CI 的
硬门槛，因为仓库 CI 尚未统一编排跨平台服务进程、Redis fixture 和端口生命周期。

HTTP listener 最小存活 smoke 可按 `deployment/http-ingress.md` 执行：临时设置
`HTTP_API_Setting.Force_Enable: true`，启动 CasterService，再访问：

```text
GET http://127.0.0.1:8080/api/status/health
```

该检查只证明 HTTP 进程和 listener 可用，不证明 Redis、Master 正确性或登录后 API
完整可用。Redis 可用时仍应优先运行 `deploy/scripts/e2e_smoke.ps1` 或
`deploy/scripts/e2e_smoke.sh`。

Redis 命令兼容 smoke 用于部署和 Redis 相关任务：

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=6379 bash deploy/scripts/check_redis_compat.sh
```

Windows：

```powershell
.\deploy\scripts\check_redis_compat.ps1 -HostName 127.0.0.1 -Port 6379
```

该检查要求目标 Redis 为 8.4.0+，并实测 `HSETEX`、`HEXPIRE`、`HTTL`
和 `SET ... IFEQ ... EX`。

## 后续任务要求

```text
NC-005 HTTP 多节点入口契约
  已由 deployment/http-ingress.md 明确 Force_Enable / master-only 部署 smoke 说明；
  Redis 可用时继续复用 e2e_smoke。

前端 lint 债务
  必须单独建任务修复现有 npm run lint 错误，然后再把 Web lint 升级为 CI 硬门槛。

NC-006 Redis 版本/命令兼容
  已由 deployment/redis.md 和 deploy/scripts/check_redis_compat.* 明确 Redis
  8.4.0+、HSETEX/HEXPIRE/HTTL/SET IFEQ 检查；Redis 可用环境必须执行并记录结果。

NC-007 Auth Online_Protection
  已补 Auth_Verify.yml 解析和实名连接数策略 schema_smoke；NC-017 已用 Redis
  8.6.3 fixture 覆盖真实 NTRIP client 登录写入/断连清理 ACT:SESSION；NC-018
  已补 Online_Protection=true 拒新与 false 踢旧的真实 NTRIP 连接矩阵；NC-019
  已补真实连接存活期间 ACT:SESSION/ACT:REC/USR:REC 续期长跑；NC-021 已补
  单节点匿名登录允许/拒绝矩阵；NC-022 已补本地双实例 AUTH:BROADCAST 踢旧连接。
  NC-023 已补禁用/失效账号矩阵；NC-024 已补 HTTP/API Redis 断线重连；NC-025
  已补本地双实例 node identity/cluster。真实跨主机网络分区和 relay failover 仍需
  后续专项补测。

NC-008B/NC-009 活跃账号 REST/SSE 读侧
  NC-016 已用 Docker Redis 8.6.3 fixture 自动验证 /api/accounts/active 与 SSE
  account_actives 初始快照同源读取 ACT:SESSION:* + STR:ACTIVE fallback。NC-017 已补
  真实 NTRIP/Auth client 登录写入与断连清理 ACT:SESSION:* 的 e2e；NC-018 已补
  Online_Protection 踢线矩阵对 /api/accounts/active 的回归；NC-019 已补续期长跑
  对 /api/accounts/active 的回归；NC-020 已补 account_actives 运行中新增/更新/删除
  SSE 增量推送；NC-021 已补匿名 client 不进入 /api/accounts/active 的回归；
  NC-022 已补本地双实例 AUTH:BROADCAST 后两实例 /api/accounts/active 读侧一致。
  NC-023 已补禁用/失效账号拒绝后不进入 /api/accounts/active 的回归；NC-024 已补
  HTTP API 短断后 Redis 连接状态恢复；NC-025 已补本地双实例 node identity/cluster。
  真实跨主机入口、反代/sticky session 和 relay failover 仍需专项覆盖。

NC-010 Proto/API/Web 类型同步
  已新增 tools/contract_check/check_api_contracts.mjs，并接入主 CI。
  改 proto、HTTP JSON 或 web/src/api/types.ts 时必须运行并记录结果。
```
