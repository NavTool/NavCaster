# NavCaster v2 Web Layout Reference

更新时间：2026-06-27
任务：NC-081 v2 Architecture API Data Contract
状态：v2 冻结契约
适用范围：v2 Web 控制面信息架构、页面布局、Sub2API 参考边界和操作语义。
可信度：架构冻结文档；后续 Web skeleton 和页面实现必须按本文执行。

依据：

```text
F:\Projects\NavCaster\shared\references\sub2api-structure-design-notes-2026-06-26.md
F:\Projects\NavCaster\shared\references\2026-06-26-control-plane-data-plane-architecture-plan.md
F:\Projects\NavCaster\shared\references\2026-06-27-large-refactor-implementation-blueprint.md
```

## 1. 设计结论

v2 Web 是全新管理前端：

```text
不沿用旧 NavCaster Web 风格。
不复用旧 Web 页面组织作为约束。
不兼容旧 HTTP API。
不直接复制 Sub2API 业务页面或 Vue SFC 代码。
参考 Sub2API 的后台管理台布局、信息密度、侧边导航、表格页、筛选分页、弹窗编辑、状态标签和指标卡。
NavCaster v2 页面字段、路由语义、API 和业务动作按 v2 契约重新定义。
```

v2 Web 的首屏应是可用的控制台，不做营销式 landing page。

## 2. Sub2API 只读参考边界

允许参考：

```text
应用壳：侧边栏 + 顶部栏 + 主内容区。
侧边导航：品牌区、角色区分、分组菜单、折叠态和 active route。
顶部栏：页面标题、用户菜单、移动端菜单按钮、关键操作入口。
表格页：actions / filters / table / pagination 四段式布局。
通用组件：DataTable、Pagination、Dialog、ConfirmDialog、StatCard、StatusBadge、SearchInput、DateRangePicker。
管理台页面组合：Dashboard、Users、Accounts、Usage、OpsDashboard 的布局节奏。
三角色 route scope 的组织方式。
统一 apiClient、auth store 和 route meta 的职责拆分。
```

禁止照搬：

```text
OpenAI / Claude / Gemini 模型路由。
token 成本、图片计费、模型映射、LLM 平台 quota。
Claude Code 限制。
OAuth 上游账号调度。
Sub2API 支付平台和订单流程。
Sub2API 的 Vue SFC 源码。
Sub2API 的业务字段名和页面语义。
Sub2API 的 Ent/后端实现细节。
```

参考文件清单：

```text
F:\Temporary\sub2api\frontend\src\components\layout\AppLayout.vue
F:\Temporary\sub2api\frontend\src\components\layout\AppSidebar.vue
F:\Temporary\sub2api\frontend\src\components\layout\AppHeader.vue
F:\Temporary\sub2api\frontend\src\components\layout\TablePageLayout.vue

F:\Temporary\sub2api\frontend\src\components\common\DataTable.vue
F:\Temporary\sub2api\frontend\src\components\common\Pagination.vue
F:\Temporary\sub2api\frontend\src\components\common\BaseDialog.vue
F:\Temporary\sub2api\frontend\src\components\common\ConfirmDialog.vue
F:\Temporary\sub2api\frontend\src\components\common\StatCard.vue
F:\Temporary\sub2api\frontend\src\components\common\StatusBadge.vue
F:\Temporary\sub2api\frontend\src\components\common\SearchInput.vue
F:\Temporary\sub2api\frontend\src\components\common\DateRangePicker.vue

F:\Temporary\sub2api\frontend\src\views\admin\DashboardView.vue
F:\Temporary\sub2api\frontend\src\views\admin\UsersView.vue
F:\Temporary\sub2api\frontend\src\views\admin\AccountsView.vue
F:\Temporary\sub2api\frontend\src\views\admin\UsageView.vue
F:\Temporary\sub2api\frontend\src\views\admin\ops\OpsDashboard.vue
```

说明：

```text
Sub2API 前端是 Vue 3 / Pinia / TailwindCSS。
NavCaster v2 若采用 React，必须转译布局结构和交互意图，不复制 Vue 实现。
```

## 3. Web 技术边界

建议：

```text
TypeScript / React。
集中 route registry。
route meta 只表达认证、角色、标题、功能门禁。
导航配置独立为 nav.config.ts。
统一 apiClient 处理 token、错误、响应解包和 request_id。
运行态数据使用 runtime / worker / session / host 等专门 store 或 query hooks。
表格页复用统一 layout、筛选、分页、批量操作和弹窗编辑组件。
```

禁止：

```text
在页面组件里硬编码大段导航结构。
让 Web 直接拼接 Redis key。
让 Web 根据旧 API shape 自行兼容。
把控制面动作显示为立即成功。
```

## 4. 路由信息架构

角色 scope：

```ts
type RoleScope = 'admin' | 'customer' | 'supplier';
type AllowedRole = 'admin' | 'customer' | 'supplier';
```

路由建议：

```text
/login

/admin/dashboard
/admin/control/hosts
/admin/control/runtimes
/admin/control/runtimes/:runtime_id
/admin/control/config
/admin/control/audit
/admin/accounts
/admin/access-accounts
/admin/mount-point-groups
/admin/mount-points
/admin/stations
/admin/usage
/admin/billing
/admin/supplier-usage
/admin/settings

/dashboard
/access-accounts
/groups
/mount-points
/online-sessions
/usage
/profile

/supplier/dashboard
/supplier/access-accounts
/supplier/stations
/supplier/online-stations
/supplier/supply-usage
/supplier/earnings
/supplier/profile
```

控制面页面可全部放在 `/admin/control/*` 下。若实现保留 `/control/*` 内部 route，也必须在用户可见入口上收敛到 admin 管理台语义。

## 5. 应用壳

v2 管理台应用壳：

```text
左侧侧边栏。
顶部栏。
主内容区域。
可折叠导航。
移动端抽屉导航。
顶部显示当前页面标题、环境、用户菜单和关键状态。
```

侧边栏分区：

```text
Overview
Control Plane
Access
Assets
Usage & Billing
Supplier
Audit
Settings
```

状态表达：

```text
Host online / agent_offline / disabled。
Runtime desired_state 和 actual_state 并列展示。
Config published / draft / rollback。
Worker running / draining / stopped。
Intent accepted / pending / applying / failed / completed。
```

## 6. 页面规格

### 6.1 Admin Dashboard

应展示：

```text
Runtime 总数、running 数、failed 数。
Host 总数、agent_offline 数。
总连接数、source 数、client 数。
worker loop delay p95。
Redis connected 状态。
待处理 intent 和 failed event。
容量建议和告警。
```

### 6.2 Hosts

表格字段：

```text
host_id
hostname
agent_status
os
arch
cpu_usage
memory_usage
runtime_count
last_heartbeat_at
actions
```

操作：

```text
enable / disable host
view host detail
create runtime on host
```

### 6.3 Runtimes

表格字段：

```text
runtime_id
name
host
desired_state
actual_state
config_version
listen_port
worker_count
connections
loop_delay_p95
updated_at
actions
```

操作：

```text
create runtime
start
stop
restart
drain
undrain
change worker_count
publish config
```

操作提示：

```text
按钮提交的是 intent。
提交后显示 accepted / pending。
actual_state 变化后再显示完成或失败。
```

### 6.4 Runtime Detail

分区：

```text
Summary
Desired vs Actual
Workers
Mount Owners
Sessions
Metrics
Events
Config
Actions
```

必须并列展示：

```text
desired_state
actual_state
desired config_version
actual config_version
desired worker_count
actual worker_count
last desired update
last actual update
```

### 6.5 Worker Metrics

表格字段：

```text
worker_id
state
draining
mount_count
source_count
client_count
fanout_per_sec
fanout_cost_p95
loop_delay_p95
redis_publish_count
slow_client_disconnect_count
```

操作：

```text
drain worker
view mounts
view sessions
```

### 6.6 Config

页面能力：

```text
config version list
draft create / edit
diff current published vs draft
publish
rollback
release status per runtime
```

配置发布后不显示为“立即生效”，而显示：

```text
published
release pending
agent applying
runtime reloaded / restarted
failed
```

## 7. 操作模型

Web 操作必须是意图：

| Web 操作 | API | UI 状态 |
| --- | --- | --- |
| Create Runtime | `POST /api/v1/control/runtimes` | `intent accepted` -> `starting` -> `running/failed` |
| Start | `POST /actions/start` | `pending` -> `running/failed` |
| Stop | `POST /actions/stop` | `pending` -> `stopped/failed` |
| Restart | `POST /actions/restart` | `restarting` -> `running/failed` |
| Drain | `POST /actions/drain` | `draining` -> `drained/running` |
| Change worker_count | `PUT /desired-state` | `pending` -> actual worker count converged |
| Publish config | `POST /config-versions/{id}/publish` | `published` -> release progress |

Web 不得：

```text
显示“远程进程已启动”，除非 actual_state 已确认。
把 HTTP 202 当作最终完成。
在 Agent 离线时允许用户误以为动作会立即执行。
```

## 8. API 模块

建议拆分：

```text
web/src/api/client.ts
web/src/api/auth.ts
web/src/api/control/hosts.ts
web/src/api/control/runtimes.ts
web/src/api/control/config.ts
web/src/api/control/audit.ts
web/src/api/admin/accounts.ts
web/src/api/admin/accessAccounts.ts
web/src/api/me/*
web/src/api/supplier/*
web/src/api/types.ts
```

通用 apiClient 负责：

```text
Authorization header。
request_id 透传或生成。
错误 envelope 解包。
401 / 403 统一处理。
JSON 序列化。
取消请求和超时。
```

## 9. 组件边界

基础组件：

```text
AppShell
Sidebar
Header
TablePageLayout
DataTable
Pagination
BaseDialog
ConfirmDialog
StatCard
StatusBadge
SearchInput
DateRangePicker
ActionMenu
MetricSparkline
StateTimeline
DesiredActualPanel
```

设计要求：

```text
后台管理台应保持高信息密度和清晰层级。
表格页优先支持筛选、分页、排序和批量操作。
状态字段使用 Badge，不用长段说明文字。
危险操作需要 ConfirmDialog 和审计 reason。
Runtime/Worker 指标使用紧凑卡片和表格，不做营销式 hero。
```

## 10. 权限和越权

Web 路由权限只是第一层 UX 控制，后端必须强制鉴权。

规则：

```text
/admin/* 仅 admin。
/supplier/* 允许 supplier，admin 可作为超集进入。
/dashboard 等 customer 页面允许 customer，admin 可作为超集进入。
me API 不接受 account_id。
supplier API 不接受 supplier_account_id。
admin API 必须显式目标 ID。
```

越权负例需要进入 QA：

```text
customer 直接访问 /admin/control/runtimes。
supplier 查询其他供应商 supply usage。
customer 在 body 中传入其他 account_id。
非 admin 调用 Runtime start/stop。
Agent local runtime API 被浏览器访问。
```

## 11. 后续验证要求

后续 Web 实现至少覆盖：

```text
npm run build。
路由权限负例。
API envelope 解包。
Runtime action 显示 pending 而不是直接成功。
desired / actual state 并列展示。
Agent offline 时动作禁用或明确 pending。
Config publish release progress。
Sub2API 参考边界检查：未复制 Vue SFC，未带入 LLM 业务页面。
桌面和移动端布局截图。
```
