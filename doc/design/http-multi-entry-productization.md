# HTTP 多入口产品化设计决策

更新时间：2026-06-16
任务：NC-040 HTTP Multi Entry Productization Design
依据：NC-032 HTTP ingress / sticky session / 写路由策略 e2e，NC-038 runtime image provenance，`doc/deployment/http-ingress.md`，`doc/current/backend-http.md`

## 设计结论

NavCaster 当前产品化选择继续支持“固定管理入口”或“稳定 sticky session 管理入口”，不把多个 HTTP 节点包装成无状态 round-robin 写入口池。

这不是最终的分布式管理 API 方案，而是当前代码和验证证据能够承诺的产品边界：

- HTTP token 保持进程内 session map，不写 Redis，不跨节点共享。
- `/api/events/stream` 使用同一 token 认证，和 REST API 一样需要命中签发 token 的节点。
- Web 管理台面向一个稳定 `baseURL` 工作，不做多后端自动漂移。
- 多节点写操作只能通过一个逻辑管理入口进入：固定单节点，或具备稳定 affinity 的 sticky 入口。
- 普通 round-robin 只可作为故障演示或负面测试场景，不作为受支持的写入口部署形态。

推荐部署优先级：

| 优先级 | 入口模式 | 适用条件 | 产品承诺 |
| --- | --- | --- | --- |
| 1 | Master-directed 固定入口 | 反向代理或外部编排能识别当前 Redis master holder | Web/API 写操作集中到当前控制节点，最贴近现有 master-only 默认模型。 |
| 2 | 单一固定管理节点 | 现场需要稳定入口，且只在一个明确节点设置 `Force_Enable: true` | 管理会话稳定；但该节点绕过 master 启动限制，不代表写操作自动串行到 leader。 |
| 3 | 稳定 sticky 管理入口 | LB 能保证同一浏览器或管理客户端持续命中同一上游 | 登录、REST 和 SSE 会话稳定；Redis 持久化配置写入后其他节点可读到一致结果。 |

默认配置仍保持 `HTTP_API_Setting.Force_Enable: false`。如果现场使用 sticky 管理入口并让多个节点暴露 HTTP，必须把它理解为“有状态管理会话池”，而不是“无状态写 API 池”。

## 不选方案

| 方案 | 本轮不选理由 | 未来触发条件 |
| --- | --- | --- |
| 分布式 HTTP session/token | 需要新增 Redis token store、TTL/续期、登出撤销、token 哈希存储、跨节点 SSE 认证、重启语义和安全审计；会改变当前 401/403 边界。 | 需要真正支持无状态 round-robin REST/SSE，并能接受 token 存储与安全模型变更。 |
| 入口 master gating | 当前 HTTP listener 启动后不会因 master 丢失自动关闭；仅加中间件拒写会引入读写分类、503/leader hint、Web 降级和 failover UX 设计。 | 需要把“只有 master 接受管理写操作”变成产品契约，并补动态 master lease gate。 |
| 统一写 leader 路由 | 需要节点间 HTTP/RPC 转发、认证上下文传递、审计 actor 保真、幂等键、超时/重试/循环保护和 leader failover 处理；对所有写 API 的语义分类成本高。 | 需要任意入口都可接收写请求，且非 leader 节点能可靠代理到 leader。 |
| 让 Web 自动轮询多个后端 | 会和进程内 token、SSE 长连接、浏览器重连和权限错误处理冲突；当前 `probeBackend()` 只证明 health 存活。 | 分布式 token 或 leader routing 落地后，再设计 Web 多入口发现与切换。 |

因此，本轮设计不修改业务源码、脚本或 API 行为，只把支持边界产品化并同步文档口径。

## API 影响

当前选型不新增、不删除、不重命名 HTTP API。

对调用方的约束是：

- `POST /api/auth/login` 可命中任意可用 HTTP 节点，但返回 token 只在该节点有效。
- 登录后所有受保护 REST 请求必须继续命中签发 token 的节点。
- `/api/status/health` 仍是公开 liveness，只能证明 HTTP listener 存活，不能证明 Redis、master、token 或写入口正确。
- `/api/status`、`/api/monitor/cluster` 等登录后状态接口可用于人工或运维编排判断 `node_id`、`master_node` 和 Redis 连接状态。
- 对写 API 不应配置跨上游自动重试。尤其不要在 `POST`、`PUT`、`DELETE` 收到 401/403 或 upstream error 后透明换节点重放，因为当前没有全局幂等键。

未来若实现 master gating 或 leader routing，API 需要补充统一错误语义，例如 `503` 或 `409` 加 `leader_node_id` / `retry_after_ms` / `request_id`。这些不属于当前支持面。

## Token 与安全影响

当前 token 继续是进程内 Bearer token：

- 不写入 Redis，不跨节点复制。
- 服务重启后失效。
- logout 只撤销本进程 token。
- SSE query token 和 `Authorization: Bearer` 均按当前实现保留。

部署和观测不得记录原始 token。需要排查 round-robin 问题时，可以记录 token 哈希前缀、客户端会话 key、上游地址、HTTP status 和 `node_id`，不要把完整 Authorization header 写入日志。

## SSE 影响

SSE 与 REST API 使用相同 token 边界：

- EventSource 必须通过同一个固定或 sticky 管理入口建立。
- 如果 LB 使用 cookie、header 或源地址作为 sticky key，SSE 重连必须继续携带同一 key。
- round-robin 下 SSE 可能在重连后命中未签发 token 的节点并返回 401/403；这是入口策略错误，不是 SSE channel 数据源错误。
- SSE channel 数据仍按当前 `sse_manager` 周期快照广播；本设计不改变 channel 名称、轮询周期或 payload。

## Web 影响

Web 管理台当前只支持一个稳定管理入口：

- `baseURL` 应指向固定入口或 sticky 入口。
- 登录后 REST、SSE 和页面刷新都必须沿用同一入口。
- `probeBackend()` 只调用 `/api/status/health`，不能用它判断 token 是否仍有效，也不能用它选择多个后端。
- 浏览器看到登录后请求间歇性 401/403，且刷新或重登后短时间恢复，优先按 ingress sticky 失效或 round-robin 误配排查。

未来若要支持多入口自动切换，需要先落地分布式 token 或 leader routing，再补 Web 侧 baseURL 发现、SSE 重连和错误提示设计。

## 部署影响

受支持部署必须满足以下条件之一：

- 反向代理固定指向当前有效 master 或一个明确管理节点。
- 反向代理提供稳定 sticky session，且登录、REST、SSE 都使用同一 sticky key。
- 运维直接访问某个明确节点的 HTTP 入口，并理解 token 不能跨节点复用。

反向代理配置建议：

- health check 使用 `/api/status/health` 作为进程 liveness。
- master 或写入口正确性使用登录后的 `/api/status`、`/api/monitor/cluster`、Redis `CASTER:MASTER` 或外部编排状态判断。
- access log 至少保留 upstream address、status、method、path、request id、sticky key 哈希或会话标识哈希。
- 不要把多个 `Force_Enable: true` 节点作为普通 round-robin upstream 暴露给 Web。
- 不要用 `navcaster:latest` 作为 Docker bridge 或 HTTP ingress QA 证据；使用当前 commit 生成的 `navcaster:team-dev-<short12>` 并记录 OCI revision label。

## 普通 round-robin 限制和告警口径

普通 round-robin 写入口是明确不支持的部署形态。

已知现象：

- 登录请求成功后，后续请求可能交替命中不同节点。
- 命中签发 token 的节点返回 200，命中其他节点返回 401/403。
- NC-032 中 nginx round-robin 入口复用登录 token 时出现过 `401,200,401,200...` 的稳定边界。
- 如果客户端或代理对写请求做跨节点自动重试，可能造成重复写、半完成感知或审计链路不清。

告警和运维措辞建议：

```text
HTTP ingress misconfiguration: protected API requests for one management session
are reaching multiple upstream NavCaster nodes. NavCaster HTTP tokens are
process-local. Use a fixed management entry or stable sticky session; ordinary
round-robin write ingress is unsupported.
```

建议触发条件：

- 同一客户端会话 key 或 token 哈希在短窗口内命中多个 upstream。
- 同一路径族的受保护 API 在同一管理会话中出现 200 与 401/403 交替。
- Web 登录后 SSE 或 `/api/status` 间歇性 401/403，同时 `/api/status/health` 持续 200。
- 写 API 401/403 比例上升且 upstream 分布不稳定。

告警级别：

- 生产 Web/API 写入口：按部署错误处理，建议 P1。
- 测试环境或专门运行 `-IncludeHttpIngressStrategy` 负面用例：记录为预期现象，不作为服务故障。
- 单节点或固定入口偶发 401/403：不按 round-robin 结论处理，应继续排查 token 过期、服务重启、用户 logout 或凭据问题。

## 后续可拆分任务

可独立拆分的后续任务：

| 任务方向 | 主要交付 |
| --- | --- |
| Ingress 诊断增强 | 在状态、日志或审计中暴露安全的 `node_id`、request id、上游诊断信息，帮助识别 sticky 失效。 |
| 分布式 HTTP session/token | Redis token store、TTL/续期、logout 撤销、token 哈希、跨节点 REST/SSE 验证、重启语义和安全文档。 |
| Master gating | 写 API 分类、master lease 中间件、非 master 错误语义、Web 提示、failover 后恢复验证。 |
| 统一写 leader 路由 | leader discovery、请求转发、认证上下文、审计 actor、幂等键、超时重试、循环保护和失败语义。 |
| Web 多入口 UX | baseURL 发现、sticky 失效提示、SSE 重连策略、重新登录和权限错误区分。 |
| 部署手册硬化 | nginx/Traefik/Kubernetes ingress sticky 示例、TLS/反代头、provenance image 证据模板。 |
| QA 矩阵产品化 | 把 fixed/sticky、round-robin 负面、master failover、SSE reconnect、写 API 幂等风险拆成可重复 gate。 |

## 最低验证矩阵

| 场景 | 最低验证 | 通过标准 |
| --- | --- | --- |
| 本设计文档任务 | `git diff --check`、`git diff --name-status team-dev...HEAD`、`git diff --stat team-dev...HEAD`、关键词 `rg` | diff 只包含文档；无 whitespace error；口径能搜到 HTTP ingress、sticky、round-robin、session、leader。 |
| 当前 fixed/sticky 产品口径 | `deploy/scripts/e2e_smoke.ps1 -IncludeHttpIngressStrategy -NavCasterImage navcaster:team-dev-<short12>` | 直连跨节点 token 拒绝、round-robin 负面边界、sticky 连续请求固定到同一 node_id、sticky 写入后双直接入口读取一致。 |
| Runtime image provenance | `build_runtime_image.sh` 与 `docker image inspect navcaster:team-dev-<short12>` | 记录 image id、`org.opencontainers.image.revision` 和对应 commit；不得使用 `navcaster:latest` 作为证据。 |
| Master 与 cluster 基线 | `-IncludeDockerBridgeCluster` 或既有 master lease/failover smoke | 两节点 node_id 不同，cluster 视图一致，master failover 后 survivor 收敛。 |
| SSE sticky 回归 | 在 sticky 入口订阅 `/api/events/stream` 并触发至少一个已覆盖 channel 变化 | SSE 连接不因 upstream 漂移产生 401/403；重连仍命中同一 sticky 后端。 |
| 若实现分布式 token | 新增 round-robin REST/SSE 正向矩阵 | 登录 token 可跨节点验证，logout/restart/TTL 语义明确，token 不以明文持久化。 |
| 若实现 master gating | 新增非 master 写 API 拒绝矩阵 | 非 master 写请求返回统一错误，leader/master 信息准确，failover 后写入口恢复。 |
| 若实现 leader routing | 新增任意入口写 leader 转发矩阵 | 非 leader 接收写请求可转发到 leader，审计 actor 保真，重复/超时/leader 切换有确定语义。 |

## 残余风险

- 真实生产 LB 的 sticky 算法、TLS 终止、代理头和超时策略尚未由自动化覆盖。
- sticky 入口只能稳定管理会话，不能保证任意节点都具备 leader 写路由能力。
- 当前 HTTP listener 不随 master 丢失自动下线，master-directed 模式依赖外部编排或后续 master gating。
- Web 仍无法区分“凭据失效”和“LB 把请求送到非签发节点”的所有场景。
- 写 API 尚无统一幂等键，代理层不应跨节点自动重放写请求。
