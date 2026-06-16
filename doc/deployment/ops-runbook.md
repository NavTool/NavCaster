# NavCaster Ops Runbook

更新时间：2026-06-17

本文是部署和值班故障处理入口，覆盖 NC-029 至 NC-040 已自动化验证或产品化决策的
master lease、cluster 节点视图、HTTP ingress、relay failover、Redis 运行契约、
Linux runtime image provenance、HTTP 多入口边界和 CI smoke matrix。细节命令以
`qa/qa-gates.md`、`deployment/http-ingress.md`、`design/http-multi-entry-productization.md`
和 `deployment/redis.md` 为准。

## 快速分流

| 症状 | 先看 | 关键证据 | 常用 smoke |
| --- | --- | --- | --- |
| 管理入口偶发 401/403 | HTTP token 归属节点、入口是否 round-robin | 登录节点、后续请求命中节点、sticky key | `-IncludeHttpIngressStrategy` |
| 集群显示双 master 或 master 不收敛 | `CASTER:MASTER`、`/api/status.master_node`、`/api/monitor/cluster.master_node` | Redis holder、两个 HTTP 入口状态、retired node 是否仍 master | `-IncludeMasterLeaseFailover`、`-IncludeMasterLeaseStability` |
| 节点缺失或 node_id 冲突 | `/api/status.node_id`、`/api/monitor/cluster` | `node_id`、`hostname`、`listen_port`、`http_port`、`process_id`、`http_enabled` | `-IncludeLocalDualNodeIdentity`、`-IncludeDockerBridgeCluster` |
| relay stop 后仍显示 running | HTTP status、Redis `PULL:STAT`/`PUSH:STAT`、cluster pull/push count | status state、`connect_key`、`node_uid`、cluster count | `-IncludeRelayPullStartStop`、`-IncludeRelayPushStartStop` |
| master 切换后 pull relay 未恢复 | `CASTER:MASTER`、`PULL:STAT`、target auth session、cluster pull count | 新 master node_id、新 `connect_key`、retired `node_uid` 不再 running | `-IncludeRelayFailover` |
| master 切换后 push relay 未恢复收包 | `CASTER:MASTER`、`PUSH:STAT`、target source、cluster push count、下游 client payload | 新 master node_id、新 relay `connect_key`、retired `node_uid` 不再 running、failover 后 payload 收到 | `-IncludeRelayPushFailover` |
| Redis 短断后 API 异常 | `/api/status/health`、`/api/status` Redis 连接字段 | 进程存活、`redis_caster_connected`、`redis_auth_connected` | `-IncludeRedisReconnect` |
| Docker/bridge smoke 证据不可追溯 | runtime image tag 和 OCI labels | image id、revision label、当前 commit | `build_runtime_image.sh` + `-NavCasterImage` |

## 已验证事实

### Master lease 与 cluster 视图

- Master lease 使用 Redis `CASTER:MASTER`。首次抢占依赖 `SET ... NX EX`，续租依赖
  `SET ... IFEQ ... EX`，因此生产 Redis 最低版本为 8.4.0+。
- NC-029 已验证本地双实例中停止当前 master 后，survivor 接管；Redis
  `CASTER:MASTER`、`/api/status.master_node` 和 `/api/monitor/cluster.master_node`
  收敛到 survivor node_id。retired node 在 TTL grace 内可以仍显示 online，
  但不得继续标为 master。
- NC-030 已验证停止 standby 不会导致 master 抖动，standby 恢复后不抢占当前
  master，停止当前 master 后 survivor 接管，retired master 恢复后不得出现双 master。
- NC-025 已验证同主机双实例会生成不同 `Node_XXXXX`，并在两个 HTTP 入口的
  `/api/monitor/cluster` 中显示两个 online node。
- NC-031 已验证 Docker bridge 隔离网络中两个 Linux runtime 容器节点的
  `node_id` 不同，cluster payload 中 `hostname`、`listen_port`、`http_port`、
  `process_id` 和 `http_enabled` 与对应容器一致，并覆盖 master 停止后的 survivor
  接管。

值班判断：

```text
1. 用 Redis 读 CASTER:MASTER，确认当前 holder。
2. 登录可用 HTTP 入口，读取 /api/status.master_node。
3. 读取 /api/monitor/cluster.master_node 和 nodes 列表。
4. 如果 Redis holder 与 HTTP/cluster 不一致，先等待一个 lease/heartbeat 窗口；
   仍不一致时记录 Redis key、两个 HTTP 响应和相关进程/容器状态。
```

### HTTP ingress fixed/sticky 策略

- `HTTP_API_Setting.Force_Enable=false` 时，HTTP API 只在本节点成为 Redis master 后启动。
- `Force_Enable=true` 会绕过 master 判断直接启动 HTTP API；当前 listener 一旦启动，
  不会因后续 master 丢失自动关闭。
- HTTP token 是进程内会话，不写 Redis、不跨节点共享。
- NC-032 已验证直接跨节点复用 token 会返回 401/403；nginx round-robin 入口复用
  token 会暴露登录后请求命中不同节点的边界；稳定 sticky key 可以把同一管理会话
  固定到同一 node_id。
- 当前推荐多节点管理入口为单一固定管理入口，或具备稳定 sticky session 的管理入口。
  不支持把多个 `Force_Enable=true` 节点作为无状态 round-robin 写入口池。
- NC-040 已把该结论产品化为设计决策：本轮不选择分布式 HTTP session/token、
  入口 master gating、统一写 leader routing 或 Web 多后端自动漂移；如需这些能力，
  必须按 `design/http-multi-entry-productization.md` 拆出后续实现任务。

处置建议：

```text
1. 偶发 401/403 时，先确认是否通过普通 round-robin 入口访问。
2. 固定管理入口部署中，确认代理目标仍是预期节点。
3. sticky 部署中，确认 sticky key/cookie/header 在登录和后续 API 请求中稳定传递。
4. /api/status/health 只证明 HTTP listener 存活，不证明 Redis、master 或 token 有效。
```

### Relay failover 与状态收敛

- Relay 调度由 master 通过 `NODE:<node_id>` 发布任务指令；运行状态写入
  `PULL:STAT` 和 `PUSH:STAT`。
- NC-026/NC-027 已验证 pull/push start/stop 本地真实链路，状态判断必须同时看
  HTTP status、Redis status 和 cluster pull/push count。
- NC-026 post-merge 修复过 stale running 状态回写问题：INACTIVE 后按 uid 删除状态，
  周期上报只续期本节点真实持有的 relay 状态，同步时拒绝恢复本节点无真实连接的
  running stale 状态。
- NC-028 已验证 pull 与 push relay 的本地真实数据内容转发。
- NC-033 已验证 pull relay failover：停止当前 master/executor 后，survivor 成为
  Redis、status 和 cluster 一致的 master；retired `node_uid` 不再作为 running 证据；
  pull relay 在 survivor node_id 上用新的 `connect_key` 重新 running。
- NC-039 已验证 push relay failover：停止当前 master/executor 后，survivor 成为
  master；旧 target source 消失，retired `node_uid` 不再作为 running 证据；同一
  push relay 在 survivor node_id 上用新的 relay `connect_key` 重新 running，且
  下游 client 能收到 failover 后的 payload。

处置建议：

```text
1. relay running 证据至少包含 state=1、非空 connect_key、node_uid 与当前 executor 一致。
2. stop 或 cleanup 后，不要只看 API 返回；必须确认 HTTP status、Redis PULL:STAT/PUSH:STAT
   和 cluster pull/push count 都收敛。
3. failover 后，如果 status 仍指向 retired node_uid，记录 Redis status 原文、
   CASTER:MASTER holder、survivor /api/status 和 cluster payload。
```

### Redis TTL、key 和监控建议

- Redis 生产最低版本是 Open Source 8.4.0+，当前 Docker、CI 和 Linux package 默认
  验证版本为 8.6.3。
- 必需命令包括 `HSETEX`、`HEXPIRE`、`HTTL` 和 `SET ... IFEQ ... EX`。项目没有
  `HSET` + key TTL 或 Lua 兼容降级路径。
- 关键运行态 key：
  - `CASTER:MASTER`：master holder，字符串 TTL。
  - `CASTER:NODE`：节点视图，hash field TTL。
  - `MPT:STAT`、`USR:STAT`、`STR:STAT`：在线 server/client/stream 状态。
  - `PULL:STAT`、`PUSH:STAT`：relay 运行态。
  - `ACT:REC:<account>`、`ACT:UND:<name>`、`ACT:SESSION:<account>`、`USR:REC:<account>`：
    Auth/Core 在线连接与展示会话。
  - `NODE:HISTORY:<node_id>`、`MONITOR:REDIS:HISTORY`：监控历史，使用 list trim。
- NC-019 已用 `HTTL` 验证实名在线会话续期中 `ACT:SESSION`、`ACT:REC`、
  `USR:REC` 三处 field TTL 为正。
- NC-024 已验证 Redis 短断期间 `CasterService` 进程保持存活，`/api/status/health`
  仍可响应；Redis 恢复后 `/api/status` 中 caster/auth Redis 连接恢复，并可重新登录。

监控建议：

```text
1. 对 CASTER:MASTER 监控 TTL 和 holder 变化。
2. 对 CASTER:NODE field 数量、field TTL 和 node payload 的 http_enabled/process_id 做巡检。
3. 对 PULL:STAT/PUSH:STAT running 项监控 node_uid、connect_key 和更新时间。
4. 对 ACT:SESSION/ACT:REC/USR:REC 监控 field TTL，排查续期或清理异常。
5. 对 MONITOR:REDIS:HISTORY 和 NODE:HISTORY:* 只作为近期观测数据，不作为长期审计库。
```

### Linux runtime image provenance

- NC-038 后 Linux runtime image 必须从当前 commit 构建不可变 tag，默认形如
  `navcaster:team-dev-<short12>`。
- `deploy/scripts/build_runtime_image.sh` 会写入 `org.opencontainers.image.revision`、
  `version`、`source`、`created` 等 OCI labels，并打印 `NAVCASTER_IMAGE`。
- `deploy/docker/docker-compose.yml` 要求显式设置 `NAVCASTER_IMAGE`；没有默认当前证据 tag。
- Docker bridge cluster 和 HTTP ingress strategy smoke 必须显式传入
  `-NavCasterImage navcaster:team-dev-<short12>`。脚本会拒绝把旧 mutable tag
  当作运行态证据。

证据模板：

```text
commit:
NAVCASTER_IMAGE:
docker image id:
org.opencontainers.image.revision:
org.opencontainers.image.version:
runtime smoke:
```

## CI smoke 矩阵

主 CI 硬门槛：

```text
API contract check
Web build
schema_smoke Ninja build
CTest schema_smoke
Linux package build
Windows package build
```

本地准入入口：

```powershell
.\deploy\scripts\admission_check.ps1 -BuildType Release
```

运行态 smoke 按风险触发：

| 场景 | 命令入口 | 何时触发 |
| --- | --- | --- |
| 默认 HTTP/Redis/NTRIP fixture | `-RedisMode Docker -Configuration Release` | HTTP、Redis、服务启动链路改动 |
| Master failover | `-IncludeMasterLeaseFailover` | master lease、cluster、node 状态改动 |
| Master stability | `-IncludeMasterLeaseStability` | lease 边界、standby 恢复、双 master 风险 |
| Docker bridge cluster | `-IncludeDockerBridgeCluster -NavCasterImage ...` | runtime image、容器网络、cluster 视图改动 |
| HTTP ingress | `-IncludeHttpIngressStrategy -NavCasterImage ...` | 反向代理、HTTP token、写入口策略改动 |
| Relay failover | `-IncludeRelayFailover` | relay 调度、master/executor 关系改动 |
| Relay push failover | `-IncludeRelayPushFailover` | push relay 调度、target source、failover 后收包恢复改动 |
| Relay data forwarding | `-IncludeRelayDataForwarding` | relay 数据路径、session I/O 改动 |
| Redis reconnect | `-IncludeRedisReconnect` | Redis adapter、连接恢复、状态 API 改动 |

## 残余风险

- Docker bridge cluster 不是跨物理主机或真实生产网络分区验证；主机防火墙、跨机 DNS、
  L4/L7 LB、TLS 终止和生产 sticky 算法仍需现场或专项验证。
- HTTP token 仍是进程内会话；固定/sticky 管理入口不是分布式无状态 session 方案。
- 当前 HTTP listener 启动后不会随 master lease 丢失自动关闭；严格动态入口需要后续设计。
- NC-033/NC-039 已覆盖 pull 与 push relay failover 的本地双实例证据；长时间大吞吐、
  丢包/乱序、跨主机网络异常和复杂 LB 组合仍是残余风险。
- Redis 当前继续承载配置、运行态和近期历史监控；长期审计、容量规划和历史归档策略仍需
  后续架构决策。
- `STR:ACTIVE` 仍作为 `/api/accounts/active` legacy fallback；完全清理需确认无存量依赖。
- Web lint 仍有既有基线债，主 CI 当前不把 lint 作为硬门槛。
