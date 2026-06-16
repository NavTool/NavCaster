# HTTP API 部署契约

更新时间：2026-06-16

基线：NC-035 复核，覆盖 NC-005 与 NC-032 后的当前实现事实。

产品化决策：NC-040 明确继续支持固定管理入口或稳定 sticky session 管理入口；
普通 round-robin 写入口池不属于当前支持能力。详细取舍、未选方案、告警口径和
后续拆分任务见 `doc/design/http-multi-entry-productization.md`。

## 当前实现事实

HTTP API 与 SSE 运行在 `ntrip_caster` 的独立 `_http_base` 线程中，监听配置来自
`HTTP_API_Setting`。当前模板显式写出：

```yaml
HTTP_API_Setting:
  Port: 8080
  Bind_Addr: "0.0.0.0"
  CORS_Origin: "*"
  Force_Enable: false
```

`Force_Enable` 默认是 `false`：

- `false`：HTTP API 只在本节点成为 Redis 选举出的 Master 后启动。
- `true`：跳过 Master 判断，进程初始化时直接启动 HTTP API。

当前 listener 一旦启动，不会因为后续 Master 丢失而自动关闭。该行为来自
`Http_Gate_Callback` 的现有实现，部署侧不能假设 HTTP 入口会随 Master
切换自动下线。后续如需严格的动态租约入口，应在集群/master lease 任务中另行设计。

## 推荐部署口径

### 单节点

生产单节点建议保持 `Force_Enable: false`，让 HTTP API 跟随 Master 状态启动。若现场
没有可用 Redis 或只是做本地 smoke，可临时设置 `Force_Enable: true`，用完后恢复。

### 多节点

推荐默认策略是 `Force_Enable: false`，并让反向代理或运维入口指向当前有效 Master。
这样 Web/API 操作集中在集群控制节点，避免多个节点同时暴露管理写入口。

反向代理至少需要具备以下约束：

- 只把 Web/API 流量转发到当前可访问且符合运维预期的节点。
- 不把多个 `Force_Enable: true` 的节点作为无状态负载均衡池。
- 把 `/api/status/health` 作为 HTTP 进程存活检查，而不是 Master 正确性检查。
- 使用登录后的 `/api/status`、节点列表或外部编排状态判断当前 Master 和 Redis 状态。

### 固定管理节点

如果现场需要固定管理入口，可以只在一个明确选定的节点设置 `Force_Enable: true`，
反向代理固定指向该节点。该模式适合运维入口稳定性优先的部署，但它绕过 Master
限制，不能被理解为集群写操作自动串行化方案。

不要在多个节点上同时设置 `Force_Enable: true` 后再用普通轮询负载均衡暴露给 Web。
这样会增加管理操作落到非预期节点、状态判断不一致和故障排查复杂度。

### 反向代理与 sticky session

NC-032 增加了 Docker bridge + nginx smoke，用两个隔离网络内的 NavCaster 节点验证
反向代理入口边界：

- HTTP 登录 token 仍是进程内会话，不会通过 Redis 在多个节点间共享。
- 普通 round-robin 入口会把登录后请求转到另一节点，导致 token 被 401/403 拒绝。
- 使用稳定 sticky key 的反向代理入口可以把同一管理会话固定到同一节点，登录、
  `/api/status` 和集群查询可以稳定工作。
- 账号等 Redis 持久化业务数据可通过固定/sticky 管理入口写入，随后两个直接节点入口
  都能读取到一致结果；删除后两个节点也都应读到 404。

因此，当前推荐的多入口写操作策略仍是“单一固定管理入口”或“具备稳定 sticky session
的管理入口”。不要把多个 HTTP 节点作为无状态 round-robin 写入口池；如需真正的无状态
多入口管理 API，后续必须补充分布式 HTTP session、入口 master gating 或统一写 leader
路由设计。

普通 round-robin 入口只作为负面验证或故障排查场景存在。生产告警可按如下口径处理：
同一管理会话的登录后 REST/SSE 请求在多个 upstream 间漂移，并出现 200 与 401/403
交替时，应判定为 HTTP ingress sticky/fixed 策略错误。代理层不得对 `POST`、`PUT`
或 `DELETE` 写请求做跨节点透明重试。

## 兼容性边界

本契约不改变现有 HTTP API 路径、SSE 通道、Redis schema、NTRIP 监听端口、
账号鉴权语义或 CasterCore 行为。NC-005 只把已有 `Force_Enable` 行为写入配置模板
和部署说明。

`Port` 仍参与 node_id 计算，因此同主机多实例需要使用不同 HTTP 端口，避免节点身份
冲突。NC-025 已用本地双实例 smoke 验证两个不同 HTTP/NTRIP 端口的实例会得到不同
`Node_XXXXX`，并能同时出现在两个 HTTP 入口的 `/api/monitor/cluster` 中。

## QA smoke

### 最小 HTTP 存活 smoke

该 smoke 用于确认服务二进制、配置模板和 HTTP listener 可运行。它可以在 Redis
不可用的本地环境中执行，但应临时启用 `Force_Enable: true`：

```powershell
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target CasterService

# 在生成后的 bin\Release\conf\Service_Setting.yml 中临时设置：
# HTTP_API_Setting:
#   Force_Enable: true

bin\Release\CasterService.exe
curl.exe -fsS http://127.0.0.1:8080/api/status/health
```

预期结果：返回 `{"status":"ok"}`，服务进程保持运行。smoke 结束后恢复
`Force_Enable: false` 或删除临时运行目录。

### Redis 可用时的 e2e smoke

当 Redis、`curl` 和 `jq` 可用，并且 CasterService 已经监听 HTTP 端口后，运行：

```bash
BASE=http://127.0.0.1:8080 USER=admin PASS=admin bash deploy/scripts/e2e_smoke.sh
```

该脚本覆盖登录、状态、集群、审计、ring log、Redis 历史和系统事件等关键 API。
多节点默认策略下应保持 `Force_Enable: false`，并确保当前节点已经成为 Master 或
反向代理已经指向有效入口。

### 反向代理/sticky session smoke

先从当前 commit 构建带 provenance 的 Linux runtime image，并记录脚本输出的
`NAVCASTER_IMAGE`：

```bash
bash deploy/scripts/build_runtime_image.sh
```

当本机已有 `navcaster:team-dev-<short12>`、`redis:8.6.3` 和 `nginx:latest`
镜像时，可运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\deploy\scripts\e2e_smoke.ps1 `
  -RootPath . `
  -IncludeHttpIngressStrategy `
  -NavCasterImage navcaster:team-dev-<short12> `
  -StartupTimeoutSec 60
```

Docker bridge cluster smoke 同样必须显式传入当前 commit image：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\deploy\scripts\e2e_smoke.ps1 `
  -RootPath . `
  -IncludeDockerBridgeCluster `
  -NavCasterImage navcaster:team-dev-<short12> `
  -StartupTimeoutSec 60
```

QA 记录应写明 runtime image tag、`docker image inspect` 的 image id、
`org.opencontainers.image.revision` label 和对应 `team-dev` commit；不得使用
`navcaster:latest` 作为本轮证据 tag。

该 smoke 会创建独立 Docker bridge 网络、Redis、两个 NavCaster 容器和一个 nginx 容器；
验证直连跨节点 token 拒绝、round-robin token 边界、sticky 管理入口稳定性，以及通过
sticky 入口写入/删除账号后两个节点读取结果一致。脚本结束后会清理容器、网络和临时配置。
