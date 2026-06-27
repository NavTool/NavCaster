# Web 管理台当前实现说明

更新时间：2026-06-27
来源：NC-035 frontend 文档审计、NC-056 Web 三角色改造、NC-060 订阅兑换运营、`web` 源码和近期任务记录。

## 当前事实

当前 Web 管理台位于 `web/`，不是旧资料中的 `app/CasterWeb/`。技术栈以 `web/package.json` 为准：

```text
Vite 5
React 18
TypeScript
Ant Design 6
Axios
React Router
Recharts
```

`web/README.md` 仍是 Vite 模板说明，不作为项目文档入口。

## 路由和认证

`web/src/router.tsx` 使用 `HashRouter`。`/login` 公开，登录仍提交兼容
`POST /api/auth/login`，成功后按返回 subject 跳转到角色首页。其余页面经过
`RequireAuth`，守卫会读取 `GET /api/v1/auth/session`，并保存
`account_id`、`username`、`role`、`status`、`compat_admin` 和脱敏 `account` 快照。

NC-056 后 Web 路由按三角色分区：

```text
/admin/*
  仅 role=admin 可访问。默认 /admin/dashboard。
  覆盖运营总览、Account、AccessAccount、MountPointGroup、MountPoint、Subscription、RedeemCode、Station、Usage、SupplyUsage。

/me/*
  role=user 或 role=admin 可访问。默认 /me/dashboard。
  覆盖用户资料、用户接入账号 CRUD、授权分组、可用挂载点、订阅、兑换记录和计费用量。

/supplier/*
  role=supplier 或 role=admin 可访问。默认 /supplier/dashboard。
  覆盖供应商资料、基站接入账号 CRUD、供应站点、供应时长和收益。
```

管理员是超集角色。`MainLayout` 顶部提供 admin / user / supplier scope 切换；普通
user 只看到用户入口，supplier 只看到供应商入口。前端路由守卫只负责体验边界，最终权限仍由
后端 `/api/v1/admin`、`/api/v1/me`、`/api/v1/supplier` namespace 和 session
subject 校验。

旧管理台页面保留在 `/admin/legacy/*`，用于继续访问既有运行监控和旧配置页面：

```text
/admin/legacy/dashboard
/admin/legacy/nodes/:id
/admin/legacy/servers / server detail
/admin/legacy/clients / client detail
/admin/legacy/accounts / account detail
/admin/legacy/sources / aliases / sourcetable
/admin/legacy/access / access group detail
/admin/legacy/relay/pull / relay/push
/admin/legacy/history / detail
/admin/legacy/statistics
/admin/legacy/monitor
/admin/legacy/audit
/admin/legacy/logs/ring
/admin/legacy/settings
```

`web/src/api/client.ts` 使用 Axios 注入 `Authorization: Bearer <token>` 和 `X-Auth-User`。baseURL、token、authUser 保存在 localStorage。

`probeBackend()` 仍保留给旧调用方，但路由认证不再依赖公开 `/api/status/health` 判断
token 有效性。

## 运营域 API

NC-056 新增 `web/src/api/operations.ts`，只使用既有后端契约：

```text
/api/v1/auth/session
/api/v1/admin/accounts
/api/v1/admin/access-accounts
/api/v1/admin/operations-monitor
/api/v1/admin/online-connections
/api/v1/admin/audit
/api/v1/admin/mount-point-groups
/api/v1/admin/mount-points
/api/v1/admin/subscriptions
/api/v1/admin/redeem-codes
/api/v1/admin/stations
/api/v1/admin/usage
/api/v1/admin/data-push-configs
/api/v1/admin/data-push-jobs
/api/v1/admin/data-push-maintenance
/api/v1/admin/data-push-usage
/api/v1/admin/supply-usage
/api/v1/admin/supplier-settlements
/api/v1/me/profile
/api/v1/me/dashboard
/api/v1/me/allowed-groups
/api/v1/me/mount-points
/api/v1/me/access-accounts
/api/v1/me/subscriptions
/api/v1/me/usage
/api/v1/me/data-push/configs
/api/v1/me/data-push/jobs
/api/v1/me/data-push
/api/v1/me/redeem-redemptions
/api/v1/supplier/profile
/api/v1/supplier/dashboard
/api/v1/supplier/access-accounts
/api/v1/supplier/stations
/api/v1/supplier/supply-usage
/api/v1/supplier/settlements
/api/v1/supplier/earnings
```

自助 AccessAccount 表单不提交 `owner_account_id` 和 `kind`，只提交
`access_account_id`、`username`、`password`、`mount_point_group_id`、`status`、
`concurrency_limit`、`expire_time`、`private_remark` 等允许字段。后端仍会从
session 推导 owner 和 kind。

NC-057 起运营台增加 `/admin/data-push-usage`，用户自助台增加 `/me/data-push`。
两个页面按当前 `yyyyMM` 展示 `DATA:PUSH:<period>` 事实；用户页只展示当前
session Account，管理员页展示全局记录并在总览中汇总数据推送扣费。

NC-062 起运营台增加 `/admin/data-push-configs` 和 `/admin/data-push-jobs`。
管理员可创建/删除推送配置并查看全局任务；用户 `/me/data-push` 扩展为数据推送工作区，
同时展示可用配置、自己的任务和扣费用量，并可创建按配置计价的推送任务。

NC-064 起数据推送配置表单支持 `ledger_only` / `relay_push` 执行模式。
`relay_push` 配置可填写源挂载点、远端 Caster host/port、远端挂载点、账号、密码和协议。
管理员和用户任务表展示执行模式、relay uid、relay 状态和任务状态；后端响应会剥离
`relay_target_password` / `target_password`，Web 不展示远端密码。

NC-065 起管理员数据推送任务表增加完成、失败、重试、取消操作；用户数据推送任务表增加
重试、取消操作。用户操作仅作用于当前 session Account 的 relay_push 任务。

NC-066 起管理员和用户数据推送任务表增加运行态同步操作，并展示最近同步节点和同步时间，
便于运营追踪 relay_push 任务当前执行快照。

NC-067 起管理员和用户数据推送任务表展示自动维护时间、异常持续时间和失败原因。
当后台 runtime maintenance 将 relay_push 任务标记为 failed 时，运营台可直接看到
失败原因和最近维护时间。

NC-068 起管理员数据推送任务页增加 runtime maintenance 配置控件。管理员可在任务表上方
切换自动维护、调整执行周期和失败阈值，并可对当前账期立即执行一次维护；操作分别调用
`GET/PUT /api/v1/admin/data-push-maintenance` 和
`POST /api/v1/admin/data-push-maintenance?action=run&period=<yyyyMM>`。

NC-069 起运营台增加 `/admin/operations-monitor`。该页面调用
`GET /api/v1/admin/operations-monitor?period=<yyyyMM>`，展示账号余额/状态风险、
DataPush 失败/运行/维护状态、供应待结算和待付款收益，并提供告警、风险账号和最近失败
DataPush 任务表。它使用后端聚合快照，不替代旧 `/admin/legacy/monitor` 的节点/Redis
系统监控。

NC-074 起 `/admin/operations-monitor` 增加告警策略配置。页面调用
`GET/PUT /api/v1/admin/operations-alert-policy`，可调整低余额阈值、DataPush 失败阈值、
供应商待付款和待结算阈值，并可单独启停负余额、低余额、DataPush 失败、DataPush
维护关闭、供应商待付款和供应待结算告警。保存后下一次运营监控聚合立即使用新策略。

NC-070 起运营台增加 `/admin/online-connections` 和 `/admin/audit`。在线连接页调用
`GET /api/v1/admin/online-connections`，展示 `ACT:SESSION:*` 与 legacy `STR:ACTIVE`
fallback 合并后的连接、账号、类型、地址、分组和在线时长；审计日志页调用
`GET /api/v1/admin/audit`，支持 actor、action、target_type 和 limit 过滤，展示
`LOG:AUDIT` 分页结果。旧 `/admin/legacy/audit` 仍保留作为旧管理台入口。

NC-060 起运营台增加 `/admin/subscriptions` 和 `/admin/redeem-codes` 的可操作页面。
管理员可创建订阅、禁用订阅、创建兑换码，并把兑换码兑换到指定 Account。用户自助台增加
`/me/subscriptions` 和 `/me/redeem-redemptions`，只展示当前 session Account 的
订阅权益和兑换记录。

NC-061 起运营台增加 `/admin/supplier-settlements`，管理员可按当前账期查看结算批次，
并输入 Supplier Account ID 创建结算。NC-063 起该页面可把结算批次标记为已付款或
付款失败，并展示付款方式、流水和付款时间。供应商自助台增加 `/supplier/settlements`，
`/supplier/earnings` 同时展示结算批次、供应事实以及待结算、待付款、已付款和付款失败
收益汇总。

## API Types 和契约

前端类型集中在 `web/src/api/types.ts`。类型与 proto/HTTP JSON 的同步由 `api/api-contract-sync.md` 和 `tools/contract_check/check_api_contracts.mjs` 约束。

历史拼写例如 `SourceDecordType`、`SourceRecord.identufier` 属于现有契约，不能只在前端单独修正。需要改名时必须同步 proto、HTTP JSON、Web types、API 文档和契约检查。

当前允许差异：

```text
SourceRecord ecef_x/ecef_y/ecef_z
  proto-only。

AccountActive connect_key/anonymous/auth_type/group_uid
  TS-only HTTP 扩展，来源于 ACT:SESSION:<account> JSON。

NC-056 运营域类型
  TS-only HTTP 类型，来源于 NC-053/NC-054 `/api/v1/*` JSON，不对应 proto 消息。
```

## SSE 和轮询

`web/src/hooks/useSSE.ts` 提供单 channel 和多 channel EventSource 封装。`useSSE(channel)` 默认请求 `channels=<channel>`，避免误订阅全部频道；`useMultiSSE()` 用逗号拼接多频道。

断线后当前实现为指数退避重连，1 秒起步，上限 30 秒。`fallbackInterval` 目前只是选项声明，没有实际执行轮询 fallback；不要把它写成“断线后自动轮询回退”。

页面数据模式是 SSE 和 polling 混用：

```text
偏实时页面
  dashboard、servers、clients、node/detail 等使用 SSE。

CRUD/详情页面
  accounts、sources、access groups、relay 配置等大量使用 3s/5s polling。
```

## 构建和 lint

命令：

```bash
cd web
npm ci
npm run lint
npm run build
```

`npm run build` 实际执行 `tsc -b && vite build`。NC-056 在任务 worktree 中通过
`npm --prefix web run build`；Vite 仍报告既有 `api/index.ts` dynamic import 静态导入重叠和
大 chunk 警告。

前端仍有既有 lint 基线债，不能把 lint 写成当前硬门槛。NC-056 已清理本次新增/修改范围内的
lint error；剩余 lint failure 位于旧文件，例如 `api/client.ts`、`components/DataTable.tsx`、
`pages/AuditLog.tsx`、`pages/Dashboard.tsx`、`pages/RingLog.tsx`、`pages/Settings.tsx`
和 `pages/SystemMonitor.tsx`。

## 历史资料

旧 `references/historical/frontend/casterweb-progress.md` 是迁移历史，包含旧 `app/CasterWeb` 路径、JWT 说法和旧路由数量。它不属于当前实现事实。
