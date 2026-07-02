# Web 控制台当前实现说明

更新时间：2026-07-02
来源：NC-111 Web v2-only 拆分。

## 当前事实

生产态 Web 控制台位于 `app/web/`。旧混合 Web 已归档到
`.archive/v1/web/`，不再作为生产构建入口。

`app/web` 是 v2-only Vite / React / TypeScript 控制面，默认进入
`#/admin/control/hosts`，只包含：

```text
Hosts
Runtimes
Runtime detail
Workers
Config versions
```

旧 dashboard、旧三角色后台、旧 `/admin/legacy/*`、旧登录页、旧 API fallback
和 demo/mock adapter 不在 `app/web` 中。

## 路由和 Session

`app/web/src/router.tsx` 使用 `HashRouter`：

```text
/                         -> /admin/control/hosts
/v2/*                     -> /admin/control/hosts
/admin/control/hosts
/admin/control/runtimes
/admin/control/runtimes/:id
/admin/control/workers
/admin/control/config
```

`app/web/src/api/session.ts` 只读取真实 `GET /api/v1/auth/session`，用于顶部
session 展示；页面进入不依赖旧 `RequireAuth`。AdminService 权限仍由后端真实
API 返回 401/403 或业务错误。

`app/web/src/api/client.ts` 使用 Axios 调用真实 AdminService。默认同源请求；
如需跨域开发，可设置：

```text
VITE_NAVCASTER_ADMIN_BASE_URL=http://127.0.0.1:18080
```

Bearer token 从 localStorage 的 `navcasterToken` 或兼容键 `token` 读取。

## v2 AdminService API

`app/web/src/v2/api/adminService.ts` 只调用真实 v2 API：

```text
GET  /api/v1/control/hosts
GET  /api/v1/control/runtimes
GET  /api/v1/control/runtimes/{runtime_id}
PUT  /api/v1/control/runtimes/{runtime_id}/desired-state
POST /api/v1/control/runtimes/{runtime_id}/actions/{action}
```

前端不再提供 `VITE_NAVCASTER_V2_MOCK`、mock data 或 demo adapter fallback。
Workers 和 Config versions 页面保留真实空态，等待 AdminService 后续补充 API。

## 验证命令

```powershell
cd app\web
npm ci
npm run lint
npm run build
```

打包和准入脚本从 `app/web` 构建，发布包内静态目录仍为 `web/`，对应服务端
`Web_Root: "./web"` 约定。
