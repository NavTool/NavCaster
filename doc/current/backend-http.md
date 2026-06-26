# Backend HTTP 当前实现说明

更新时间：2026-06-27
来源：NC-035 backend-http 文档审计、`src/http` 源码、NC-032/NC-034/NC-053/NC-057/NC-060 任务记录。

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

- HTTP token 是进程内 Bearer token，不是 JWT。服务端生成 64 位十六进制随机字符串，存放在进程内 session map，不写 Redis、不跨节点共享；服务重启后失效。NC-054 起 session map 保存 `username/account_id/role/status/compat_admin` subject。
- SSE endpoint 是 `/api/events/stream`。路由层允许 query token，但 raw handler 内部仍校验 `?token=<token>` 或 `Authorization: Bearer <token>`。
- SSE 当前 channel：`servers`、`clients`、`streams`、`nodes`、`accounts`、`sources`、`aliases`、`access_groups`、`pull_records`、`pull_states`、`push_records`、`push_states`、`account_actives`。
- `channels` 为空、纯空白或 `*` 表示订阅全部；普通 CSV channel 按精确集合匹配。
- SSE timer 每 2 秒只轮询当前有订阅者的 channel，数据变化时广播；SSE client 上限为 200。
- `/api/accounts/active` 当前受路由注册顺序影响，实际依赖 `AccountController::get_account("active")` 兼容分支；后续重构不能删除该兼容，除非同步调整路由顺序并补测试。
- HTTP Redis adapter 已有断线重连 timer。源码当前退避约为 `1/4/6/8/10` 秒，上限 10 秒。
- `/api/status/health` 只证明 HTTP listener 存活，不证明 Redis、Master、token 或登录后 API 完整可用。
- NC-053 新增 `/api/v1/auth/session` 和 `/api/v1/admin/*` 运营域 API，读写
  NC-051 的 `ACC:*` / `AACC:*` / `MPGRP:*` / `SUB:*` / `BILL:*` /
  `SUPPLY:*` / `STATION:*` key。该 namespace 不替换旧 `/api/*`，也不切换
  NTRIP Auth 或 Web 页面。
- NC-054 新增 `/api/v1/me/*` 和 `/api/v1/supplier/*` 自助 API，并扩展
  `/api/auth/login`：优先用 `ACC:USERNAME` / `ACC:RECORD` 登录三角色 Account，
  失败后再走旧 admin / Redis admin 兼容登录。真实 Account token 返回
  `compat_admin=false`，旧 admin 兼容 token 返回 `role=admin`、`account_id=""`、
  `compat_admin=true`。
- NC-054 起 HTTP handler 对非公开路径执行 role authorizer：`/api/v1/admin/*`
  和旧 `/api/*` 只允许 admin；`/api/v1/me/*` 允许 user/admin；
  `/api/v1/supplier/*` 允许 supplier/admin。自助 API owner 只能从 session
  subject 推导，body 中的 `owner_account_id` / `kind` 不可信。
- NC-057 新增数据推送用量 API：`GET /api/v1/admin/data-push-usage` 查询全局
  `DATA:PUSH:<yyyyMM>`；`GET/POST /api/v1/me/data-push` 只作用于当前
  session Account。POST 会覆盖请求体 `account_id`，按 `actual_debit_cents`
  写 `ACC:BALANCE:LEDGER:<yyyyMM>` 并同步 `ACC:RECORD.balance_cents`；余额不足
  返回 409 且不留下半写事实。
- NC-062 新增数据推送配置和任务 API：管理员通过
  `GET/POST /api/v1/admin/data-push-configs`、`GET/PUT/DELETE
  /api/v1/admin/data-push-configs/{config_id}` 维护 `DATA:PUSH:CONFIG`，
  通过 `GET /api/v1/admin/data-push-jobs?period=yyyyMM` 查看全局任务。
  用户通过 `GET /api/v1/me/data-push/configs` 查看 active 配置，通过
  `GET/POST /api/v1/me/data-push/jobs` 创建和查看自己的任务。任务按配置小时价
  和 `used_seconds` 计算扣费，写 `DATA:PUSH:JOB:<period>`、
  `DATA:PUSH:<period>`、余额 ledger 和 Account 余额。
- NC-060 新增订阅和兑换码运营 API：`GET/POST /api/v1/admin/subscriptions`、
  `GET/PUT/DELETE /api/v1/admin/subscriptions/{subscription_id}`、`GET/POST
  /api/v1/admin/redeem-codes`、`GET /api/v1/admin/redeem-codes/{code}` 和
  `POST /api/v1/admin/redeem-codes/{code}/redeem`。订阅更新会同步 `SUB:RECORD`
  与 `SUB:ACCOUNT:<account_id>`，删除订阅会删除 account index 以触发运行时
  subscription revoke；兑换成功会写 `REDEEM:ACCOUNT:<account_id>` 和余额
  ledger，并更新 `ACC:RECORD.balance_cents`。
- NC-060 起 `POST /api/v1/admin/accounts/{account_id}/balance-adjustments` 不再只写
  ledger，还会按 `delta_cents` 同步更新 `ACC:RECORD.balance_cents` 并刷新 owner
  下 AccessAccount 运行时余额快照。`GET /api/v1/me/subscriptions` 和
  `GET /api/v1/me/redeem-redemptions` 返回当前用户自己的订阅和兑换记录。
- NC-061/NC-063 新增并扩展供应商结算 API：`GET/POST
  /api/v1/admin/supplier-settlements`、`GET
  /api/v1/admin/supplier-settlements/{settlement_id}`、`POST
  /api/v1/admin/supplier-settlements/{settlement_id}/payment` 和
  `GET /api/v1/supplier/settlements`。管理员创建结算时聚合同供应商、同账期的
  pending `SUPPLY:USAGE`，写 `SUPPLY:EARNING:<account_id>:<period>`，并把供应事实
  标记为 `settled`；结算批次默认 `pending_payment`，管理员可标记 `paid`、
  `payment_failed` 或 `cancelled` 并记录付款方式、流水和备注。供应商收益摘要从
  `SUPPLY:USAGE` 与 `SUPPLY:EARNING` 拆分 pending、pending_payment、paid 和 failed
  金额，同时返回 `settlement_count`。

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
