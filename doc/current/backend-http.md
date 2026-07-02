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
- NC-064 起 `DATA:PUSH:CONFIG.execution_mode` 支持 `ledger_only` 和
  `relay_push`。`relay_push` 配置会在用户创建任务时额外生成受管
  `PUSH:RECORD[data_push:<job_id>]`，字段映射为
  `source_mountpoint -> login_mpt`、`relay_target_* -> target_*`，由现有
  RelayScheduler/relay_push 数据平面执行。Admin/User 任务列表会读取
  `PUSH:STAT[relay_uid]` 补充 `relay_status`、`relay_state`，并在运行态把
  queued 任务展示为 running；查询响应会递归剥离 relay target 密码。
- NC-065 起新增 DataPushJob 控制 API：
  `POST /api/v1/admin/data-push-jobs/{job_id}/control` 支持
  `cancel`、`retry`、`mark_failed`、`mark_completed`；
  `POST /api/v1/me/data-push/jobs/{job_id}/control` 仅允许当前用户对自己的
  relay_push 任务执行 `cancel` / `retry`。cancel 会禁用受管 `PUSH:RECORD` 并清
  `PUSH:STAT`；retry 会重新启用或重建受管 `PUSH:RECORD`。
- NC-066 起新增 DataPushJob 运行态同步 API：
  `POST /api/v1/admin/data-push-jobs?action=reconcile&period=yyyyMM` 可批量同步当期
  relay_push 任务，`POST /api/v1/admin/data-push-jobs/{job_id}/reconcile` 和
  `POST /api/v1/me/data-push/jobs/{job_id}/reconcile` 可同步单任务。reconcile 会把
  `PUSH:STAT[relay_uid]` 的状态、connect_key 和节点信息沉淀到
  `DATA:PUSH:JOB:<period>` 的 `relay_state_snapshot`、`relay_status`、
  `runtime_reconcile_time` 等字段；终态任务不会被重新打开。
- NC-067 起 HTTP 服务增加 DataPush runtime maintenance timer。该 timer 每 60s
  tick 当前 `yyyyMM` 账期的 relay_push DataPushJob，自动沉淀运行态快照；NC-068 起
  实际执行由 `DATA:PUSH:MAINTENANCE[default]` 配置控制。配置默认
  `enabled=true`、`interval_seconds=60`、`unhealthy_after_seconds=300`，管理员可通过
  `GET/PUT /api/v1/admin/data-push-maintenance` 查询和更新，也可通过
  `POST /api/v1/admin/data-push-maintenance?action=run&period=yyyyMM` 手动维护并覆盖
  `unhealthy_after_seconds`。连续超过阈值缺失 `PUSH:STAT` 或 `state!=1` 的
  queued/running 任务会标记为 `failed`，记录 `failure_reason` / `failure_time`，
  并禁用受管 `PUSH:RECORD`。该能力是后续告警、补偿计费和真实 relay_push e2e 的
  最小状态机基础。
- NC-069 起新增 `GET /api/v1/admin/operations-monitor?period=yyyyMM` 只读聚合接口。
  该接口不写新 key，直接汇总 `ACC:RECORD`、`SUB:RECORD`、`REDEEM:CODE`、
  `DATA:PUSH:<period>`、`DATA:PUSH:JOB:<period>`、`DATA:PUSH:MAINTENANCE`、
  `SUPPLY:USAGE:<period>` 和 `SUPPLY:EARNING:*:<period>`，返回账号余额/状态风险、
  订阅/兑换码状态、DataPush 任务状态和最近失败任务、供应事实、供应商结算付款状态及
  运营告警数组。NC-074 起告警数组按 `OPS:ALERT:POLICY` 策略计算。
- NC-078 起新增 `GET/POST /api/v1/admin/operations-alert-events` 和
  `POST /api/v1/admin/operations-alert-events/{alert_event_id}/acknowledge|resolve`。
  `GET /api/v1/admin/operations-monitor` 仍保持只读，只返回 `alert_events` 摘要；
  显式 `POST ...?action=sync&period=yyyyMM` 才会把当前 monitor alerts 按
  `opsalert:<period>:<code>` 幂等写入 `OPS:ALERT:EVENT:<period>`。事件支持
  `open`、`acknowledged`、`resolved` 状态，关闭后再次触发会重新打开并递增
  `reopen_count`。
- NC-070 起新增 `GET /api/v1/admin/online-connections` 和 `GET /api/v1/admin/audit`。
  两个接口都是只读 admin v1 alias，不新增存储 key：online connections 复用
  `AccountController::list_active_sessions()`，继续从 `ACT:SESSION:*` 合并
  `STR:ACTIVE` fallback；audit 复用 `AuditLogService::list()`，从 `LOG:AUDIT`
  返回分页结果并支持 `limit`、`cursor`、`actor`、`action`、`target` 过滤。旧
  `/api/accounts/active` 与 `/api/audit` 保持兼容。
- NC-077 起新增 `GET /api/v1/admin/runtime-rejections?period=yyyyMM`。该接口只读
  `RUNTIME:REJECTION:<period>`，用于查看 AccessAccount 登录前并发拒绝事实；未传
  period 时沿用运营接口当前的 `current` 桶兼容口径，正式账期调用应传 `yyyyMM`。
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
- NC-075 新增订阅套餐 API：`GET/POST /api/v1/admin/subscription-plans` 和
  `GET/PUT/DELETE /api/v1/admin/subscription-plans/{plan_id}`。套餐写入 `SUB:PLAN`，
  创建订阅时可传 `plan_id`，后端会把套餐 `group_ids`、`price_cents`、
  `duration_days` 和 `plan_snapshot` 固化进 `SUB:RECORD` / `SUB:ACCOUNT:<account_id>`。
  该能力提供套餐运营基础，不接真实支付或自动续费。
- NC-076 新增用户自助套餐购买和兑换入口：
  `GET /api/v1/me/subscription-plans`、`POST
  /api/v1/me/subscription-plans/{plan_id}/purchase` 和 `POST
  /api/v1/me/redeem-codes/{code}/redeem`。用户购买套餐时，account_id 从 session
  推导，后端校验套餐 active、分组有效和余额足够后，写 `SUB:RECORD`、
  `SUB:ACCOUNT:<account_id>`、`ACC:BALANCE:LEDGER:<yyyyMM>` 并同步扣减
  `ACC:RECORD.balance_cents`；余额不足时不写订阅或账本半成品。用户自助兑换同样从
  session 推导 account_id，复用兑换码核销规则并刷新余额。
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

## v2 AdminService 当前事实

NC-091 起 `app/admin/` 下的 Go `navcaster-admin` 从 foundation 内存骨架推进到真实控制面闭环：

- 配置 `NAVCASTER_ADMIN_POSTGRES_DSN` 后，启动时会连接 PostgreSQL、执行
  `app/admin/migrations/0001_v2_adminservice_foundation.sql`，并把 `hosts`、
  `agents`、`runtimes`、`runtime_desired_states`、`control_intents`、
  `operation_audit_logs`、`runtime_events` 和 `runtime_actual_snapshots`
  作为 v2 source-of-truth / 审计事实。
- 未配置 PostgreSQL 时仍使用内存仓储，只用于本地开发和 self-check 降级；配置了
  PostgreSQL 但连接或 migration 失败时服务直接启动失败，不静默回退。
- 配置 `NAVCASTER_ADMIN_REDIS_ADDR` 后，desired/action/actual/heartbeat 写入会同步
  发布 v2 Redis projection；未配置 Redis 时 projection disabled，不影响内存或 PG
  source-of-truth 路径。
- `GET /api/v1/control/runtimes` 和 `GET /api/v1/control/runtimes/{runtime_id}` 返回
  desired state、最新 Agent runtime-metrics ingest 后的 actual snapshot，以及
  `control.status`。状态值为 `converged`、`pending`、`failed`、`stale`，并带
  `reason`、`desired_version`、`observed_desired_version`、`last_error` 和
  `last_observed_at`，供 Web/QA 判断 desired/actual 是否已收敛。
- action intent 生命周期使用 `accepted`、`projected`、`observed`、`superseded`、
  `failed`。新 action 会在同一事务内 supersede 同 runtime 的旧 open intent；
  `request_id` 是幂等键，重复提交不会重复 bump desired version。Redis projection
  成功后 intent 进入 `projected`；Agent actual/events 观察到相同 desired version 后
  进入 `observed`，携带 `last_error` 或失败事件时进入 `failed`。
- `GET /api/v1/control/runtimes/{runtime_id}/intents` 返回该 Runtime 最近 action
  intent 审计记录；`GET /api/v1/control/runtimes/{runtime_id}/events` 返回最近
  runtime events。
- `POST /api/v1/agents/{agent_id}/runtime-events` 写入 `runtime_events`；
  `POST /api/v1/agents/{agent_id}/runtime-metrics` 写入
  `runtime_actual_snapshots`，并刷新 control API 的 actual state 和 intent 生命周期。
- `GET /api/v1/health` 除 PG/Redis 连接状态外，还返回 `control_plane`：repository、
  host/runtime/desired 数量、latest desired version、pending/failed intent 数量和
  stale runtime 数量。PG/Redis/control-plane 状态必须用该接口或 self-check 明确记录，
  不能只用 HTTP listener 存活作为 v2 控制面通过证据。
- `app/admin/cmd/navcaster-admin-selfcheck` 是最小 v2 API 自检：health、agent register、
  runtime create、action intent、desired polling、runtime-metrics ingest 和 control
  converged readback。
- `deploy/scripts/v2_admin_control_plane_smoke.ps1` 是 NC-101 范围的真实 PostgreSQL +
  Redis fixture smoke：create runtime、action intent、actual ingest、event query、
  PG/Redis projection consistency。

v2 AdminService 不兼容旧 `/api/*` HTTP API、旧 Redis key、旧 protobuf 或旧 Web 命名。
旧 `src/http` 服务和新 `app/admin/` 服务当前是并行边界。
