# Backend HTTP 当前实现说明

更新时间：2026-06-16
来源：NC-035 backend-http 文档审计、`src/http` 源码、NC-032/NC-034 任务记录。

## 负责范围

```text
src/http/http_server.*          libevent HTTP wrapper、路由、CORS、鉴权、静态文件。
src/http/http_handler.*         路由装配和服务持有者。
src/http/*_controller.*         JSON 解析、参数校验、HTTP status 映射。
src/http/*_service.*            聚合逻辑、监控、审计、状态、历史、源表等服务。
src/http/redis_adapter.*        HTTP event loop 上的 async Redis adapter。
src/http/sse_manager.*          SSE client 管理、频道订阅、定时快照广播。
```

## 当前事实

- HTTP token 是进程内 Bearer token，不是 JWT。服务端生成 64 位十六进制随机字符串，存放在进程内 session map，不写 Redis、不跨节点共享；服务重启后失效。
- SSE endpoint 是 `/api/events/stream`。路由层允许 query token，但 raw handler 内部仍校验 `?token=<token>` 或 `Authorization: Bearer <token>`。
- SSE 当前 channel：`servers`、`clients`、`streams`、`nodes`、`accounts`、`sources`、`aliases`、`access_groups`、`pull_records`、`pull_states`、`push_records`、`push_states`、`account_actives`。
- `channels` 为空、纯空白或 `*` 表示订阅全部；普通 CSV channel 按精确集合匹配。
- SSE timer 每 2 秒只轮询当前有订阅者的 channel，数据变化时广播；SSE client 上限为 200。
- `/api/accounts/active` 当前受路由注册顺序影响，实际依赖 `AccountController::get_account("active")` 兼容分支；后续重构不能删除该兼容，除非同步调整路由顺序并补测试。
- HTTP Redis adapter 已有断线重连 timer。源码当前退避约为 `1/4/6/8/10` 秒，上限 10 秒。
- `/api/status/health` 只证明 HTTP listener 存活，不证明 Redis、Master、token 或登录后 API 完整可用。

## HTTP ingress 口径

当前推荐的多入口管理策略是单一固定管理入口，或具备稳定 sticky session 的管理入口。不要把多个 `Force_Enable=true` 节点作为无状态 round-robin 写入口池。

原因：

- token 是进程内会话，不跨节点共享；
- 普通 round-robin 会让登录后的请求命中非签发节点并返回 401/403；
- NC-032 已验证 sticky 入口可稳定固定到同一 node_id，账号 create/delete 后两个直接入口读取一致。

NC-040 已把该口径升级为产品化设计决策：当前继续支持固定管理入口或稳定 sticky
管理入口；不选择分布式 HTTP session/token、入口 master gating 或统一写 leader
路由作为本轮方案。设计细节、未选方案理由、API/token/SSE/Web/部署影响、round-robin
告警口径和后续拆分任务见 `design/http-multi-entry-productization.md`。

## 不要误用

- 旧 `references/historical/frontend/casterweb-progress.md` 和 `archive/historical/architecture_and_optimization.md` 中的 JWT 说法是历史描述。
- `/api/events/stream` 不是无需认证接口。
- `/api/status/health` 不是 Redis/Master 健康证明。
- fixed/sticky 管理入口不是最终分布式无状态 session 方案。
- 登录后请求间歇性 401/403 且 upstream 在多个节点间漂移时，优先按普通
  round-robin 写入口误配处理；不要把它当作受支持的 HA 写入口。

## 相关文档

- `api/api-reference.md`
- `api/api-contract-sync.md`
- `design/http-multi-entry-productization.md`
- `deployment/http-ingress.md`
- `current/web-console.md`
- `qa/qa-gates.md`
