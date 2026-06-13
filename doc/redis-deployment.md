# Redis 部署契约

更新时间：2026-06-14

基线：NC-019 基于 `team-dev @ 1fb4f61`。

## 版本口径

NavCaster 生产 Redis 最低版本为 **Redis Open Source 8.4.0**。当前 Docker、
Linux 打包和 GitHub Actions 默认验证/打包版本为 **Redis 8.6.3**。

最低版本由当前运行时命令决定：

| 命令能力 | 最低 Redis | NavCaster 用途 |
|----------|------------|----------------|
| `HEXPIRE` | 7.4.0 | 给 hash field 续期，维护连接/订阅/账号在线状态 |
| `HTTL` | 7.4.0 | 读取 hash field TTL，部署兼容检查和续期 QA 使用 |
| `HSETEX` | 8.0.0 | 写入带 field TTL 的运行态 hash |
| `SET ... IFEQ ... EX` | 8.4.0 | Master lease 续约，只有当前 Master 才能刷新 TTL |

因此 Redis 7.x 或 8.0-8.3 环境不满足 NavCaster 当前集群运行要求。项目不提供
`HSET` + key TTL 或 Lua 脚本的兼容降级路径。

参考文档：

- Redis `HSETEX`：https://redis.io/docs/latest/commands/hsetex/
- Redis `HEXPIRE`：https://redis.io/docs/latest/commands/hexpire/
- Redis `HTTL`：https://redis.io/docs/latest/commands/httl/
- Redis `SET` 条件选项：https://redis.io/docs/latest/commands/set/

## 当前依赖位置

```text
src/core/src/caster_internal.cpp
  HSETEX：节点、源、连接、订阅、relay 和运行态状态写入
  HEXPIRE：连接/订阅 hash field 续期
  SET ... IFEQ ... EX：CASTER:MASTER 续约

src/auth/src/auth_verify_internal.cpp
  HSETEX：ACT:REC:* / ACT:UND:* 在线状态写入
  HEXPIRE：账号在线 field 续期
```

HTTP 后端通过同步 Redis client 读取这些运行态 hash；若 Redis 版本不兼容，HTTP
可能仍能启动，但运行态、Master 选举和登录在线状态会异常。

## 部署要求

```text
生产最低版本：Redis Open Source 8.4.0+
推荐/已验证版本：Redis 8.6.3
Docker Compose：deploy/docker/docker-compose.yml 使用 redis:8.6.3
Linux package：deploy/ci/build_in_linux.sh 默认 REDIS_VERSION=8.6.3
```

如果使用外部 Redis，部署前必须运行兼容检查脚本。检查通过前不要把该 Redis
环境接入生产 CasterService。

## 本地 e2e fixture

Windows 本地 QA 可用 PowerShell smoke 脚本自动启动一次性 Redis fixture，并把
构建产物中的三个运行时配置临时指向该 fixture：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target CasterService --config Release --parallel
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release
```

默认 fixture：

```text
Redis image: redis:8.6.3
Host: 127.0.0.1
Port: 16379
Password: password
Container name: navcaster-e2e-redis-<pid>
```

如果 `16379` 已被本机 Redis 或其他服务占用，可通过 `-RedisPort 16380` 等参数
改用空闲端口。

脚本会先通过容器内 `redis-cli` 运行兼容检查，再启动 `CasterService` 并验证
`/api/status/health`、登录、`/api/status` 和 `/api/monitor/cluster`。结束后
恢复 `bin\<config>\conf\*.yml`，停止服务并删除容器。

如使用外部 Redis，可改为：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode External -RedisHost 127.0.0.1 -RedisPort 6379
```

活跃账号读侧需要同时验证 auth Redis 中的 legacy 和新会话 key 时，追加：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeActiveAccounts
```

该检查会写入并清理：

```text
STR:ACTIVE
ACT:SESSION:<account>
ACT:ACTIVE
```

预期行为是 `/api/accounts/active` 和 SSE `account_actives` 聚合
`ACT:SESSION:*`，兼容 `STR:ACTIVE`，不把 `ACT:ACTIVE` 登录索引当作在线会话。
PowerShell 脚本通过 `redis-cli -x HSET` 从 stdin 写入 JSON seed，以避免 Windows
native 参数转发导致 JSON 双引号丢失。

真实 NTRIP/Auth 写侧会话可追加：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAuthSession
```

该检查使用同一个 Redis 8.6.3 fixture，临时关闭 rover/client 匿名登录，seed
`ACT:ACTIVE` 实名账号，打开真实 NTRIP POST source 和 GET client 连接，验证
`ACT:SESSION:<account>` 写入、`/api/accounts/active` 可读取，以及 client 断连后
对应 `connect_key` 被清理。如果 `4202` 被占用，可通过 `-NtripPort 14202`
指定空闲 NTRIP 端口。

真实 NTRIP/Auth active session 续期长跑可追加：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripAuthSessionRenewal
```

该检查默认等待 25 秒，验证真实 client 保持在线时 `ACT:SESSION:<account>` 同
`connect_key` 的 `update_time` 增长，并通过 `HTTL` 确认
`ACT:SESSION:<account>`、`ACT:REC:<account>`、`USR:REC:<account>` 三个 hash
field 仍有正 TTL；随后关闭 client 并确认三处 field 被清理。调试时可通过
`-NtripRenewalWaitSec` 调整等待窗口。

真实 NTRIP/Auth `Online_Protection` 连接数矩阵可追加：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripOnlineProtection -NtripOnlineProtectionScenario RejectNew
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release -IncludeNtripOnlineProtection -NtripOnlineProtectionScenario KickOld
```

`Online_Protection` 是启动时配置，两个场景应作为独立服务生命周期运行。脚本会
seed `connection_limit=1` 的唯一实名账号，并验证：

```text
RejectNew：Online_Protection=true，第二个同账号 client 被拒绝并关闭，ACT:SESSION/ACT:REC/USR:REC 只保留旧 connect_key。
KickOld：Online_Protection=false，第二个同账号 client 登录成功，旧 socket 被关闭，ACT:SESSION/ACT:REC/USR:REC 只保留新 connect_key。
```

当 Docker engine 或外部 Redis 不可用时，脚本会非零失败；这属于环境缺口，
不能替代 e2e 通过记录。

## 兼容检查

Bash：

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=6379 bash deploy/scripts/check_redis_compat.sh
```

带密码：

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=6379 REDIS_PASSWORD='secret' \
  bash deploy/scripts/check_redis_compat.sh
```

Windows PowerShell：

```powershell
.\deploy\scripts\check_redis_compat.ps1 -HostName 127.0.0.1 -Port 6379
```

在 Redis Docker fixture 容器内检查：

```powershell
.\deploy\scripts\check_redis_compat.ps1 -DockerContainer navcaster-e2e-redis-1234 -Password password
```

脚本会执行：

```text
PING
INFO server
HSETEX NC:COMPAT:* EX 30 FIELDS 1 field value
HEXPIRE NC:COMPAT:* 30 FIELDS 1 field
HTTL NC:COMPAT:* FIELDS 1 field
SET NC:COMPAT:* node-a NX EX 30
SET NC:COMPAT:* node-b IFEQ node-a EX 30
DEL NC:COMPAT:*
```

预期结果：

```text
[redis-compat] PASS HSETEX, HEXPIRE, HTTL, SET IFEQ EX
```

失败时按照输出处理：版本低于 8.4.0、缺少命令或 `IFEQ` 不被接受，都视为部署
不兼容。
