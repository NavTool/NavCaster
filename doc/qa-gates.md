# NavCaster QA Gates

更新时间：2026-06-14

本文档定义 `team-dev` 之后的最低验证门槛。团队级流程见
`F:\Projects\NavCaster\_team\QUALITY_GATES.md`。

## CI 硬门槛

主 CI `.github/workflows/build-and-package.yml` 当前执行：

```text
API contract check
  node tools/contract_check/check_api_contracts.mjs

Web build
  cd web
  npm ci
  npm run build

schema smoke
  cmake -S . -B build/ci-Release -DCMAKE_BUILD_TYPE=Release
  cmake --build build/ci-Release --target schema_smoke --parallel
  ctest --test-dir build/ci-Release --output-on-failure -R schema_smoke

package build
  deploy/ci/build_in_linux.sh
  deploy/ci/build_in_windows.ps1
```

API contract check 会比较 `proto/caster` 中关键 message 字段、enum 成员名和
enum 数字值与 `web/src/api/types.ts` 的同步状态。未知差异会失败；当前阶段性允许差异
记录在 `doc/api-contract-sync.md` 和脚本 allowlist 中。

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
node tools\contract_check\check_api_contracts.mjs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target schema_smoke --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure -R schema_smoke

cd web
npm ci
npm run lint
npm run build
```

Linux：

```bash
node tools/contract_check/check_api_contracts.mjs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target schema_smoke --parallel
ctest --test-dir build --output-on-failure -R schema_smoke

cd web
npm ci
npm run lint
npm run build
```

## 手动或环境依赖门槛

Windows HTTP e2e smoke 优先使用脚本自动编排 Redis fixture、临时配置
`CasterService` 并清理现场：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target CasterService --config Release --parallel
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
Core register/subscribe 和 active account REST 读侧的串联路径。它不覆盖多节点
`AUTH:BROADCAST`、`Online_Protection` 踢线矩阵或长时间续期。

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
多节点 `AUTH:BROADCAST`、匿名登录矩阵和 relay failover 仍需专项任务覆盖。

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

HTTP listener 最小存活 smoke 可按 `doc/http-deployment.md` 执行：临时设置
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
  已由 doc/http-deployment.md 明确 Force_Enable / master-only 部署 smoke 说明；
  Redis 可用时继续复用 e2e_smoke。

前端 lint 债务
  必须单独建任务修复现有 npm run lint 错误，然后再把 Web lint 升级为 CI 硬门槛。

NC-006 Redis 版本/命令兼容
  已由 doc/redis-deployment.md 和 deploy/scripts/check_redis_compat.* 明确 Redis
  8.4.0+、HSETEX/HEXPIRE/HTTL/SET IFEQ 检查；Redis 可用环境必须执行并记录结果。

NC-007 Auth Online_Protection
  已补 Auth_Verify.yml 解析和实名连接数策略 schema_smoke；NC-017 已用 Redis
  8.6.3 fixture 覆盖真实 NTRIP client 登录写入/断连清理 ACT:SESSION；NC-018
  已补 Online_Protection=true 拒新与 false 踢旧的真实 NTRIP 连接矩阵；NC-019
  已补真实连接存活期间 ACT:SESSION/ACT:REC/USR:REC 续期长跑。匿名登录、禁用账号
  和多节点 AUTH:BROADCAST 仍需后续专项补测。

NC-008B/NC-009 活跃账号 REST/SSE 读侧
  NC-016 已用 Docker Redis 8.6.3 fixture 自动验证 /api/accounts/active 与 SSE
  account_actives 初始快照同源读取 ACT:SESSION:* + STR:ACTIVE fallback。NC-017 已补
  真实 NTRIP/Auth client 登录写入与断连清理 ACT:SESSION:* 的 e2e；NC-018 已补
  Online_Protection 踢线矩阵对 /api/accounts/active 的回归；NC-019 已补续期长跑
  对 /api/accounts/active 的回归；NC-020 已补 account_actives 运行中新增/更新/删除
  SSE 增量推送。多节点场景仍需专项覆盖。

NC-010 Proto/API/Web 类型同步
  已新增 tools/contract_check/check_api_contracts.mjs，并接入主 CI。
  改 proto、HTTP JSON 或 web/src/api/types.ts 时必须运行并记录结果。
```
