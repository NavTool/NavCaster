# Redis 部署契约

更新时间：2026-06-13

基线：NC-006 基于 `team-dev @ 4f7258b`。

## 版本口径

NavCaster 生产 Redis 最低版本为 **Redis Open Source 8.4.0**。当前 Docker、
Linux 打包和 GitHub Actions 默认验证/打包版本为 **Redis 8.6.3**。

最低版本由当前运行时命令决定：

| 命令能力 | 最低 Redis | NavCaster 用途 |
|----------|------------|----------------|
| `HEXPIRE` | 7.4.0 | 给 hash field 续期，维护连接/订阅/账号在线状态 |
| `HSETEX` | 8.0.0 | 写入带 field TTL 的运行态 hash |
| `SET ... IFEQ ... EX` | 8.4.0 | Master lease 续约，只有当前 Master 才能刷新 TTL |

因此 Redis 7.x 或 8.0-8.3 环境不满足 NavCaster 当前集群运行要求。项目不提供
`HSET` + key TTL 或 Lua 脚本的兼容降级路径。

参考文档：

- Redis `HSETEX`：https://redis.io/docs/latest/commands/hsetex/
- Redis `HEXPIRE`：https://redis.io/docs/latest/commands/hexpire/
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

脚本会执行：

```text
PING
INFO server
HSETEX NC:COMPAT:* EX 30 FIELDS 1 field value
HEXPIRE NC:COMPAT:* 30 FIELDS 1 field
SET NC:COMPAT:* node-a NX EX 30
SET NC:COMPAT:* node-b IFEQ node-a EX 30
DEL NC:COMPAT:*
```

预期结果：

```text
[redis-compat] PASS HSETEX, HEXPIRE, SET IFEQ EX
```

失败时按照输出处理：版本低于 8.4.0、缺少命令或 `IFEQ` 不被接受，都视为部署
不兼容。
