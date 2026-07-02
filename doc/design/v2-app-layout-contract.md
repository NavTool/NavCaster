# NavCaster v2 App Layout Contract

更新时间：2026-07-02
任务：NC-108 v2-app-layout-contract
来源基线：`team-dev @ 131c125`
状态：v2 app layout 冻结契约，作为 NC-109 到 NC-114 的输入。
适用范围：v2 生产态物理入口、legacy archive 边界、默认构建 / smoke / package / CI / QA 路径、后续任务依赖和审查口径。
可信度：架构 / 文档准入契约；本文不表示源码已完成搬迁，不表示生产发布批准。

事实来源：

```text
任务卡：_team/tasks/active/NC-108-v2-app-layout-contract.md
任务卡：_team/tasks/active/NC-109..NC-114*.md
当前源码基线：team-dev @ 131c125
当前 CMake 根入口：CMakeLists.txt
当前 Web 路由事实：web/src/router.tsx, web/src/layouts/MainLayout.tsx
当前 v2 契约：doc/design/v2-phase3-production-contract-and-delivery-gate.md
当前 v2 QA gate：doc/qa/v2-phase3-delivery-qa-gate.md
```

相关岗位：

```text
architect
docs-planner
backend-http
backend-core
frontend
qa
reviewer
integration
```

## 1. 冻结结论

v2 app layout 的最终生产态入口唯一是：

```text
repo/
  app/
    admin/    v2 Go AdminService
    agent/    v2 Go Agent
    caster/   v2 C++ Caster Runtime
    web/      v2-only Web control plane
```

legacy v1 只允许作为历史参考归档：

```text
repo/
  .archive/
    v1/
      src/    legacy C++ / HTTP / CasterService implementation
      web/    legacy Web console and mixed legacy frontend material
```

硬性规则：

```text
app/admin、app/agent、app/caster、app/web 是 v2 唯一默认生产态入口。
.archive/v1/src 和 .archive/v1/web 只保留历史参考。
.archive/v1/* 不参与默认 CMake、Go build、Web build、smoke、package、CI、QA。
根目录不再承载生产态 admin/agent/caster/web/src。
旧 HTTP API、旧 Redis key、旧 protobuf、旧 Web 页面或旧 CasterService 不能作为 v2 app layout 通过证据。
```

NC-108 本身不搬迁产品源码；NC-109 到 NC-113 才执行搬迁、脚本更新和系统验证。

## 2. 目录职责

### 2.1 v2 生产态目录

| 目录 | 职责 | 默认验证入口 |
| --- | --- | --- |
| `app/admin` | v2 AdminService，HTTP/JSON 控制面、PostgreSQL source-of-truth、Redis projection/cache/bus。 | `go test ./...`、`go build ./cmd/navcaster-admin`、Admin PG/Redis smoke。 |
| `app/agent` | v2 Agent，本机注册、心跳、desired polling、Caster supervisor 和 actual/events/metrics 上报。 | `go test ./...`、`go build ./cmd/navcaster-agent`、Agent reconcile smoke。 |
| `app/caster` | v2 C++ Caster Runtime，多 Worker NTRIP 数据面、health/metrics、Redis Pub/Sub。 | CMake Ninja `navcaster-caster` target、self-test、NTRIP / PubSub smoke。 |
| `app/web` | v2-only Web control plane，Host/Runtime/Worker/Config 控制台和操作意图 UI。 | `npm ci`、`npm run build`、真实 AdminService browser smoke。 |

`app/*` 下的程序可以有各自内部 `cmd/`、`src/`、`configs/`、`scripts/`、`qa/`、`tests/` 等目录；这些内部目录不改变顶层入口结论。

### 2.2 legacy archive 目录

`.archive/v1/*` 的唯一用途：

```text
历史追溯。
迁移比对。
必要时人工查阅旧行为。
```

`.archive/v1/*` 禁止用途：

```text
默认构建输入。
默认 smoke 输入。
默认 package 内容。
CI 默认 job 输入。
QA 通过证据。
新功能 fallback。
v2 Web / Admin / Agent / Caster 的运行依赖。
```

若必须临时查看 `.archive`，命令或脚本必须显式写出 `.archive` 路径，并在任务记录中说明只读目的。默认脚本不得递归扫描 `.archive` 后参与构建或打包。

### 2.3 根目录允许保留的公共区域

v2 app layout 后，根目录可以继续承载跨程序公共区域：

```text
.github/          CI workflow 定义。
api/              跨语言 API / OpenAPI 资料或生成入口。
cmake/            CMake 公共模块和模板。
deploy/           部署、CI、package、smoke 脚本。
doc/              仓库唯一文档入口。
proto/            当前仍被 v2 或历史工具需要的 protobuf 资料；是否继续保留由后续契约决定。
third_party/      本地第三方依赖来源。
tools/            schema、测试、模拟器和维护工具；默认工具不得隐式依赖 .archive。
CMakeLists.txt    顶层 CMake 入口；默认只接入 app/caster v2 target 和明确保留的工具 target。
CMakePresets.json Ninja preset。
```

根目录不得在最终布局中保留以下生产态入口：

```text
admin/
agent/
caster/
web/
src/
```

如果某个后续任务因为短期兼容需要保留占位目录，必须满足：

```text
不是默认构建入口。
不含生产源码。
README 明确指向 app/* 或 .archive/v1/*。
NC-113 / NC-114 记录为风险或阻断项。
```

## 3. Web 拆分契约

当前 `web` 是旧后台和 v2 控制面的混合体，不是 v2-only Web：

```text
web/src/router.tsx 同时包含 /admin/control/* v2 路由和 /admin/legacy/* 旧管理台路由。
web/src/layouts/MainLayout.tsx 仍包含旧管理台、旧三角色后台和 legacy menu。
web/src/v2/* 是嵌在旧包里的 v2 控制面骨架。
web/package.json 名称仍为 casterweb，默认 build 会构建整个混合包。
```

NC-111 必须从混合包中拆出 `app/web`，而不是把整个 `web` 目录原样迁入：

```text
允许迁移 web/src/v2/* 中符合 v2 契约的组件、页面、样式、API contract 和必要类型。
允许新建 v2-only route/auth/session/app shell。
允许迁移必要的通用工具，但必须去除旧 API fallback 和旧页面依赖。
```

`app/web` 禁止包含或依赖：

```text
旧 dashboard、旧 /admin/legacy/*、旧根路由后台页面。
旧三角色运营 / 用户 / 供应商后台作为 v2 control plane 的默认内容。
旧 HTTP API fallback。
旧 CasterService route / SSE shape / Redis key 假设。
demo adapter 作为生产态登录或 session guard 的必要条件。
直接访问 Redis、Agent local API 或 Caster internal endpoint。
```

`app/web` 默认入口必须是 v2 控制面。允许保留开发期 mock，但必须满足：

```text
默认生产 build 不以 mock 作为 live 通过证据。
mock 开关名称、默认值和 QA 记录清楚。
真实 AdminService v2 API browser smoke 是 NC-113 的通过证据。
```

## 4. 默认构建、smoke、package、CI、QA 规则

### 4.1 构建入口

v2 app layout 后的默认构建入口：

```powershell
cd app\admin
go test ./...
go build -o ..\..\build\v2-layout\navcaster-admin.exe .\cmd\navcaster-admin

cd ..\agent
go test ./...
go build -o ..\..\build\v2-layout\navcaster-agent.exe .\cmd\navcaster-agent

cd ..\..
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250

cd app\web
npm ci
npm run build
```

CMake 顶层入口必须默认接入 `app/caster`，不得默认接入 `.archive/v1/src`。如保留 legacy CMake target，必须默认关闭、显式命名为 legacy，并且不得被 package / CI / QA 默认调用。

### 4.2 smoke 入口

默认 smoke 只允许使用：

```text
app/admin
app/agent
app/caster
app/web
deploy/scripts 中已更新到 app/* 的 v2 smoke 脚本
真实 PostgreSQL / Redis fixture
v2 Redis key/channel
v2 AdminService HTTP/JSON API
```

默认 smoke 不得使用：

```text
.archive/v1/src
.archive/v1/web
旧 CasterService
旧 HTTP route
旧 Redis key/channel
旧 Web 页面
旧 protobuf 作为通过证据
```

### 4.3 package / CI 入口

package 和 CI 必须满足：

```text
Go build 路径指向 app/admin 和 app/agent。
CMake/Ninja target 指向 app/caster 的 navcaster-caster。
Web build 路径指向 app/web，Web dist 路径指向 app/web/dist 或等价变量。
package 内容不包含 .archive/v1/src 或 .archive/v1/web。
CI 默认 job 不进入 .archive。
CI artifact 不把 legacy Web 或 legacy CasterService 打包为 v2 生产内容。
```

NC-112 负责把 deploy / CI / docs / QA 路径同步到该规则。

## 5. NC-109 到 NC-114 依赖和合并顺序

| 顺序 | 任务 | 角色 | 依赖 | 必须产出 |
| --- | --- | --- | --- | --- |
| 1 | NC-108 | architect / docs-planner | `team-dev @ 131c125` | 本文和 `doc/qa/v2-app-layout-qa-gate.md`，冻结目录契约和 gate。 |
| 2 | NC-109 | backend-http / backend-core | NC-108 | `admin -> app/admin`、`agent -> app/agent`，Go build/test/smoke 路径更新。 |
| 3 | NC-110 | backend-core | NC-108，集成时需避开 NC-109 路径冲突 | `caster -> app/caster`、`src -> .archive/v1/src`，CMake/Ninja 默认只构建 v2 caster。 |
| 4 | NC-111 | frontend | NC-108 | 拆出 v2-only `app/web`，旧混合 `web -> .archive/v1/web`，不能整包迁入。 |
| 5 | NC-112 | docs-planner / qa | NC-108，最终落地需吸收 NC-109 到 NC-111 路径 | deploy / CI / QA / doc 默认路径改到 `app/*`，`.archive` 排除规则固化。 |
| 6 | NC-113 | integration / qa | NC-108 到 NC-112 均 DEV_DONE | 合入集成并执行 v2 app layout 系统 smoke，输出 QA 记录。 |
| 7 | NC-114 | reviewer | NC-113 QA | 审查路径隔离、QA 覆盖和残余风险，输出 review 结论。 |

并行规则：

```text
NC-109、NC-110、NC-111 可在 NC-108 后并行开发，但不得互相回退对方分支。
NC-112 可提前准备文档和脚本扫描清单，但最终提交必须对齐 NC-109 到 NC-111 的实际路径。
NC-113 不得合入未 DEV_DONE 的 NC-109 到 NC-112。
NC-114 不直接修代码；阻断项以 CHANGES_REQUESTED 或 BLOCKED 返回总控。
```

推荐合并顺序：

```text
NC-108
NC-109
NC-110
NC-111
NC-112
NC-113
NC-114 review approval
```

若 NC-110 先于 NC-109 合入，NC-113 必须额外检查 Go smoke 脚本中的 Caster 可执行路径没有被旧根目录假设污染。

## 6. 每个任务最低验证

| 任务 | 最低验证 |
| --- | --- |
| NC-108 | `git diff --check`、`git status --short`，确认只改 repo 文档。 |
| NC-109 | `cd app/admin; go test ./...`、`go build ./cmd/navcaster-admin`、`cd app/agent; go test ./...`、`go build ./cmd/navcaster-agent`、`git diff --check`。 |
| NC-110 | Ninja Release `navcaster-caster` build、`navcaster-caster --self-test --worker-count 2 --self-test-duration-ms 250`、路径扫描确认 `.archive/v1/src` 未被默认 CMake 引用、`git diff --check`。 |
| NC-111 | `cd app/web; npm ci; npm run build`、路径扫描确认 `app/web` 无旧页面 / 旧 route / 旧 API fallback 默认依赖、`git diff --check`。 |
| NC-112 | `git diff --check`、deploy / CI / docs / QA 路径扫描，确认默认入口均为 `app/*` 且 package 排除 `.archive`。 |
| NC-113 | NC-109 到 NC-112 的最低验证复跑，加真实 PG/Redis + Admin + Agent + Caster + Web app-layout closed-loop smoke，输出 `_team/qa/NC-113-v2-app-layout-integration-smoke.md`。 |
| NC-114 | `git diff --stat team-dev..HEAD`、`git diff --check team-dev..HEAD`、NC-108 到 NC-113 任务 / QA 记录检查、路径隔离抽查，输出 `_team/reviews/NC-114-v2-app-layout-review-gate.md`。 |

## 7. Reviewer 抽查口径

NC-114 必须至少抽查：

```text
根目录是否仍残留生产态 admin/agent/caster/web/src。
app/admin、app/agent、app/caster、app/web 是否是默认生产态入口。
CMakeLists / CMakePresets / build_ninja 是否默认进入 app/caster，且不进入 .archive/v1/src。
Go build/test/smoke 是否从 app/admin 和 app/agent 执行。
Web package/build/preview 是否从 app/web 执行。
deploy/scripts/package_* 和 deploy/ci 是否不会打包 .archive。
.github workflows 是否不会默认构建 legacy archive。
QA 文档和实际 QA 记录是否排除旧 API/key/protobuf/Web 证据。
app/web 是否仍 import 旧页面、旧 MainLayout、旧 API client fallback 或旧 /admin/legacy route。
真实闭环 smoke 是否覆盖 app/admin + app/agent + app/caster + app/web。
未运行项是否有原因、风险、后续任务和阻断判定。
```

阻断项示例：

```text
.archive/v1/src 被默认 CMake add_subdirectory 或 schema smoke 编译。
.archive/v1/web 被默认 npm build 或 package 拷贝。
app/web 只是整个旧 web 的移动版，仍默认包含 /admin/legacy 或旧根路由后台。
CI/package 仍从根 web/dist 或根 admin/agent/caster 取生产产物。
NC-113 用旧 CasterService、旧 route、旧 Redis key 或 mock Web 作为闭环通过证据。
```

## 8. 后续更新规则

以下变更必须同步本文、`doc/qa/v2-app-layout-qa-gate.md`、任务卡和相关岗位记忆：

```text
改变 v2 默认生产态入口目录。
改变 .archive/v1 的构建、package 或 QA 排除规则。
新增需要默认构建的 v2 程序。
改变 app/web 的 v2-only 范围或 auth/session 入口。
保留任何根目录生产态 admin/agent/caster/web/src 兼容路径。
让 legacy v1 目录重新参与默认 CI、smoke、package 或 QA。
```

## 9. 待确认项

```text
proto/ 是否长期保留在根目录，还是后续拆入 app/caster 或 api 契约目录。
tools/schema_smoke 是否在 v2 app layout 后继续保留旧 schema 覆盖，或拆分为 legacy-only 显式 gate。
package 最终是否同时发布 Admin / Agent / Caster / Web，还是分 artifact 发布。
app/web 生产态 auth/session guard 的最终接口和 token 存储策略。
```

这些待确认项不阻止 NC-109 到 NC-114 执行，但不得被误写成已完成事实。
