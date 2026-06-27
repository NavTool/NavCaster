# NavCaster v2 Web Layout Reference

任务：NC-086-v2-web-control-plane-foundation
更新时间：2026-06-27
范围：v2 Web 控制面骨架、Sub2API 只读参考清单、采用/调整/舍弃点。

## 上游口径

本实现对齐 NC-081 冻结契约：

```text
F:\Projects\NavCaster\worktrees\docs-planner\NC-081-v2-architecture-api-data-contract\doc\design\v2-web-layout-reference.md
```

用户可见控制面入口收敛到：

```text
/admin/control/hosts
/admin/control/runtimes
/admin/control/runtimes/:runtime_id
/admin/control/workers
/admin/control/config
```

`/v2/*` 仅作为开发期兼容重定向到 `/admin/control/hosts`，不作为正式信息架构入口。

## Sub2API 只读参考文件

布局参考：

```text
F:\Temporary\sub2api\frontend\src\components\layout\AppLayout.vue
F:\Temporary\sub2api\frontend\src\components\layout\AppSidebar.vue
F:\Temporary\sub2api\frontend\src\components\layout\AppHeader.vue
F:\Temporary\sub2api\frontend\src\components\layout\TablePageLayout.vue
```

通用组件参考：

```text
F:\Temporary\sub2api\frontend\src\components\common\DataTable.vue
F:\Temporary\sub2api\frontend\src\components\common\Pagination.vue
F:\Temporary\sub2api\frontend\src\components\common\BaseDialog.vue
F:\Temporary\sub2api\frontend\src\components\common\ConfirmDialog.vue
F:\Temporary\sub2api\frontend\src\components\common\StatCard.vue
F:\Temporary\sub2api\frontend\src\components\common\StatusBadge.vue
F:\Temporary\sub2api\frontend\src\components\common\SearchInput.vue
F:\Temporary\sub2api\frontend\src\components\common\DateRangePicker.vue
```

管理台页面节奏参考：

```text
F:\Temporary\sub2api\frontend\src\views\admin\DashboardView.vue
F:\Temporary\sub2api\frontend\src\views\admin\UsersView.vue
F:\Temporary\sub2api\frontend\src\views\admin\AccountsView.vue
F:\Temporary\sub2api\frontend\src\views\admin\UsageView.vue
F:\Temporary\sub2api\frontend\src\views\admin\ops\OpsDashboard.vue
```

API/路由组织参考：

```text
F:\Temporary\sub2api\frontend\src\router\index.ts
F:\Temporary\sub2api\frontend\src\router\meta.d.ts
F:\Temporary\sub2api\frontend\src\api\client.ts
F:\Temporary\sub2api\frontend\src\api\admin\index.ts
```

## 采用点

```text
应用壳：侧边栏 + 顶部栏 + 主内容区。
侧边栏：品牌区、折叠菜单、active route 高亮。
顶部栏：页面标题、环境/契约标识、登录主体。
表格页：actions / filters / table / pagination 四段式结构。
通用组件：状态 Badge、指标卡、确认弹窗、表格页布局。
管理台信息密度：首屏直接显示可操作控制台，不做 landing page。
路由边界：admin-only 控制面入口。
```

## 调整点

```text
技术栈从 Vue 3 / Pinia / TailwindCSS 转为现有 React 18 / TypeScript / Ant Design。
视觉风格改为 NavCaster v2 独立浅色高密度管理台，不沿用旧 NavCaster 深色蓝灰主题。
正式入口使用 /admin/control/*，符合 NC-081 冻结路由语义。
导航项按 NavCaster Host / Runtime / Worker / ConfigVersion 重建。
Runtime 详情强调 desired vs actual 并列展示。
Web 操作统一提交 AdminService intent，不显示“进程已直接执行”。
后端未完整实现时默认使用 mock contract，可通过 VITE_NAVCASTER_V2_MOCK=false 切真实接口。
```

## 舍弃点

```text
不复制 Sub2API Vue SFC 代码。
不引入 Sub2API 的 OpenAI / Claude / Gemini / OAuth / 图片计费 / quota 页面。
不沿用 Sub2API 的业务字段名和 LLM 页面信息架构。
不复用旧 NavCaster Web 页面组织、旧组件规范或旧 HTTP API shape 作为 v2 约束。
不绕过 AdminService 直接写 Redis、PostgreSQL 或本地配置文件。
不把 HTTP intent accepted 展示成最终执行成功。
```

## 本任务落地文件

```text
web/src/v2/api/contracts.ts
web/src/v2/api/adminService.ts
web/src/v2/api/mockData.ts
web/src/v2/components/V2Shell.tsx
web/src/v2/components/V2TablePage.tsx
web/src/v2/components/V2StatusBadge.tsx
web/src/v2/components/V2MetricCard.tsx
web/src/v2/components/V2ConfirmDialog.tsx
web/src/v2/pages/V2HostsPage.tsx
web/src/v2/pages/V2RuntimesPage.tsx
web/src/v2/pages/V2RuntimeDetailPage.tsx
web/src/v2/pages/V2WorkersPage.tsx
web/src/v2/pages/V2ConfigVersionsPage.tsx
web/src/v2/v2.css
```

## 状态模型

控制面状态模型：

```text
pending
running
failed
draining
offline
```

Runtime 额外并列展示：

```text
desired_state
actual_state
desired config_version
actual config_version
desired worker_count
actual worker_count
```

ConfigVersion 发布状态展示为 release progress，不显示立即生效。
