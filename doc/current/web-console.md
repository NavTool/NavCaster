# Web 管理台当前实现说明

更新时间：2026-06-16
来源：NC-035 frontend 文档审计、`web` 源码和近期任务记录。

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

`web/src/router.tsx` 使用 `HashRouter`。`/login` 公开，其余页面经过 `RequireAuth`。当前受保护页面覆盖：

```text
dashboard
node detail
servers / server detail
clients / client detail
accounts / account detail
sources / aliases / sourcetable
access groups / access group detail
pull relay / push relay
connection history / detail
statistics
system monitor
audit log
ring log
settings
```

`web/src/api/client.ts` 使用 Axios 注入 `Authorization: Bearer <token>` 和 `X-Auth-User`。baseURL、token、authUser 保存在 localStorage。

`probeBackend()` 只请求公开 `/api/status/health`，只证明后端可达，不证明 token 有效。

## API Types 和契约

前端类型集中在 `web/src/api/types.ts`。类型与 proto/HTTP JSON 的同步由 `api/api-contract-sync.md` 和 `tools/contract_check/check_api_contracts.mjs` 约束。

历史拼写例如 `SourceDecordType`、`SourceRecord.identufier` 属于现有契约，不能只在前端单独修正。需要改名时必须同步 proto、HTTP JSON、Web types、API 文档和契约检查。

当前允许差异：

```text
SourceRecord ecef_x/ecef_y/ecef_z
  proto-only。

AccountActive connect_key/anonymous/auth_type/group_uid
  TS-only HTTP 扩展，来源于 ACT:SESSION:<account> JSON。
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

`npm run build` 实际执行 `tsc -b && vite build`。近期记录显示 NC-029 至 NC-033 已补跑 Web build 并通过，NC-034 未改 Web 源码且未运行 Web build。前端仍有既有 lint 基线债，不能把 lint 写成当前硬门槛。

## 历史资料

旧 `references/historical/frontend/casterweb-progress.md` 是迁移历史，包含旧 `app/CasterWeb` 路径、JWT 说法和旧路由数量。它不属于当前实现事实。
