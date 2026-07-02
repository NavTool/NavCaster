# NavCaster v2 App Layout QA Gate

更新时间：2026-07-02
任务：NC-108 v2-app-layout-contract
来源基线：`team-dev @ 131c125`
状态：v2 app layout QA / review gate 冻结输入，供 NC-109 到 NC-114 执行。
适用范围：`app/*` 生产入口、`.archive/v1/*` 排除规则、Web v2-only 拆分、部署 / CI / package / smoke 路径迁移和最终集成审查。
可信度：QA 准入契约；不表示本文命令已在 NC-108 执行。

## 1. Gate 结论

v2 app layout QA 只接受以下生产态入口：

```text
app/admin
app/agent
app/caster
app/web
```

以下目录只作为历史参考，不得作为默认通过证据：

```text
.archive/v1/src
.archive/v1/web
```

QA 结论不得用以下证据替代 v2 app layout 通过：

```text
旧 CasterService 构建成功。
旧 src schema smoke 成功。
旧 web build 成功。
旧 HTTP API 可用。
旧 Redis key/channel 可用。
mock Web 或 demo adapter 可用。
```

## 2. 结果规则

| 结果 | 使用条件 |
| --- | --- |
| `QA_PASSED` | 当前任务验收标准和本 gate 对应项均通过，无阻断未运行项。 |
| `QA_PASSED_WITH_NOTED_RISKS` | 关键 gate 通过，但有明确非阻断环境限制或后续风险。 |
| `QA_PARTIAL` | 只覆盖部分布局边界，未覆盖项、风险和后续任务已列明。 |
| `QA_BLOCKED` | 环境或依赖缺失导致最低门槛无法执行，且没有等价替代证据。 |
| `QA_FAILED` | 路径契约不一致、旧目录参与默认链路、构建失败或验收标准未达成。 |

NC-113 不得在以下情况下写完整 `QA_PASSED`：

```text
app/admin、app/agent、app/caster、app/web 任一默认 build/test 未运行且无任务卡批准的降级。
.archive/v1/src 参与默认 CMake、smoke、package 或 CI。
.archive/v1/web 参与默认 Web build、package 或 CI。
app/web 仍默认包含旧 Web 页面、旧路由或旧 API fallback。
真实 PG/Redis + Admin + Agent + Caster + Web 闭环未运行。
旧 API/key/protobuf/Web 行为被作为通过证据。
```

## 3. Gate 分层

| Gate | 名称 | 触发任务 | 目标 |
| --- | --- | --- | --- |
| L0 | Contract / baseline | NC-108 | 冻结目录契约、合并顺序、最低验证和 reviewer 抽查口径。 |
| L1 | Go services layout | NC-109 | `app/admin` 和 `app/agent` 成为唯一默认 Go 入口。 |
| L2 | Caster layout / archive src | NC-110 | `app/caster` 成为默认 C++ runtime 入口，`.archive/v1/src` 被排除。 |
| L3 | Web v2-only split | NC-111 | `app/web` 是 v2-only 控制面，`.archive/v1/web` 被排除。 |
| L4 | Deploy / CI / docs path sync | NC-112 | deploy、CI、package、QA 和文档默认路径全部指向 `app/*`。 |
| L5 | Integration smoke | NC-113 | 合入 NC-108 到 NC-112 后执行完整 app-layout 系统 smoke。 |
| L6 | Review gate | NC-114 | 审查物理隔离、QA 覆盖和残余风险，给出合并结论。 |

## 4. L0 Contract / Baseline

适用任务：

```text
NC-108
```

最低检查：

```powershell
git diff --check
git status --short
```

通过标准：

```text
文档明确 app/admin、app/agent、app/caster、app/web 是 v2 唯一默认生产态入口。
文档明确 .archive/v1/src 和 .archive/v1/web 只作历史参考。
文档明确 .archive 不参与默认构建、smoke、package、CI、QA。
文档明确当前 web 是旧后台和 v2 控制面混合体，NC-111 必须拆 v2-only app/web。
文档列出 NC-109 到 NC-114 的依赖、合并顺序、最低验证和 reviewer 抽查口径。
NC-108 不修改产品源码搬迁。
```

## 5. L1 Go Services Layout

适用任务：

```text
NC-109
```

目录：

```text
app/admin
app/agent
```

最低命令：

```powershell
cd app\admin
go test ./...
go build -o ..\..\build\v2-layout\navcaster-admin.exe .\cmd\navcaster-admin

cd ..\agent
go test ./...
go build -o ..\..\build\v2-layout\navcaster-agent.exe .\cmd\navcaster-agent

cd ..\..
git diff --check
```

路径扫描：

```powershell
Test-Path .\admin
Test-Path .\agent
rg -n "(\badmin\b|\.\\admin|admin/|agent/|\.\\agent)" deploy doc .github -g "*.ps1" -g "*.sh" -g "*.yml" -g "*.yaml" -g "*.md"
```

通过标准：

```text
根目录不再存在生产态 admin 或 agent 源码目录。
Go module 能在 app/admin 和 app/agent 独立测试构建。
deploy / smoke / QA 文档中的默认 Go 路径已改为 app/admin 和 app/agent，或由 NC-112 明确接手。
Agent 启动 Caster 的路径通过配置、脚本参数或 app-layout 默认值注入，不硬编码旧根 caster 路径。
```

阻断项：

```text
根 admin 或 agent 仍是默认 build/test 入口。
脚本继续从根 admin/agent 构建生产产物。
Go build 依赖相对路径穿回旧根目录。
```

## 6. L2 Caster Layout / Archive src

适用任务：

```text
NC-110
```

目录：

```text
app/caster
.archive/v1/src
```

最低命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
.\bin\Release\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250
git diff --check
```

路径扫描：

```powershell
Test-Path .\caster
Test-Path .\src
Test-Path .\app\caster
Test-Path .\.archive\v1\src
rg -n "\.archive[/\\]v1[/\\]src|add_subdirectory\\(.archive|add_subdirectory\\(src\\)|\\$\\{CMAKE_SOURCE_DIR\\}[/\\]src" CMakeLists.txt cmake deploy tools doc .github -g "*"
```

通过标准：

```text
根目录不再存在生产态 caster 或 src 源码目录。
app/caster 参与默认 CMake / Ninja v2 runtime 构建。
.archive/v1/src 不参与默认 CMake、schema smoke、package、CI。
navcaster-caster build/self-test 通过。
v2 NTRIP 和 Redis Pub/Sub smoke 脚本路径指向 app/caster 产物或 app-layout 默认产物。
```

阻断项：

```text
CMake 默认 add_subdirectory(.archive/v1/src)。
schema smoke 默认编译 .archive/v1/src 作为当前 v2 gate。
package 复制 .archive/v1/src。
navcaster-caster 目标只能通过旧根 caster 构建。
```

## 7. L3 Web v2-only Split

适用任务：

```text
NC-111
```

目录：

```text
app/web
.archive/v1/web
```

最低命令：

```powershell
cd app\web
npm ci
npm run build

cd ..\..
git diff --check
```

路径扫描：

```powershell
Test-Path .\web
Test-Path .\app\web
Test-Path .\.archive\v1\web
rg -n "admin/legacy|/legacy|旧管理台|MainLayout|Dashboard|CasterService|VITE_NAVCASTER_V2_MOCK|mock contract|demo adapter" app\web -g "*.ts" -g "*.tsx" -g "*.js" -g "*.jsx" -g "*.json" -g "*.md"
rg -n "web/dist|npm --prefix web|cd web|app/web|app\\web" deploy doc .github -g "*.ps1" -g "*.sh" -g "*.yml" -g "*.yaml" -g "*.md"
```

通过标准：

```text
根目录不再存在生产态 web 源码目录。
app/web 可 npm ci / npm run build。
app/web 默认入口是 v2 控制面。
app/web 不默认包含旧 dashboard、旧三角色后台、/admin/legacy 或旧 API fallback。
app/web 不需要 demo adapter 才能通过生产态 login/session guard。
app/web 连接真实 AdminService v2 API 的 browser smoke 可由 NC-113 执行。
```

阻断项：

```text
app/web 是整个旧 web 原样搬迁。
app/web 默认路由仍包含 /admin/legacy 或旧根管理台。
app/web build 依赖 .archive/v1/web。
生产态 app/web 只能通过 mock 或 demo adapter 运行。
Web 直接访问 Redis、Agent local API 或 Caster internal endpoint 执行动作。
```

## 8. L4 Deploy / CI / Docs Path Sync

适用任务：

```text
NC-112
```

最低检查：

```powershell
git diff --check
rg -n "npm --prefix web|web/dist|cd web|\\badmin\\b|\\bagent\\b|\\bcaster\\b|\\bsrc\\b|\\.archive" deploy doc .github CMakeLists.txt CMakePresets.json -g "*"
```

通过标准：

```text
deploy / CI 默认 Go 路径指向 app/admin 和 app/agent。
deploy / CI 默认 CMake 路径指向 app/caster 的 navcaster-caster。
Web build / dist 路径指向 app/web 和 app/web/dist 或等价变量。
package 脚本明确不打包 .archive/v1/src 或 .archive/v1/web。
doc/current、doc/qa、doc/api 中不再把根 admin/agent/caster/web/src 写成 v2 默认路径。
NC-113 可直接使用更新后的命令清单。
```

阻断项：

```text
CI 默认仍跑根 web build。
package 仍要求 root web/index.html 或 root web/dist。
deploy smoke 仍从 root admin/agent/caster 构建。
.archive 未被排除且会进入 package artifact。
```

## 9. L5 Integration Smoke

适用任务：

```text
NC-113
```

前置：

```text
NC-108 到 NC-112 均 DEV_DONE。
集成 worktree 已合入对应分支。
真实 PostgreSQL 和 Redis fixture 可用，或明确 QA_BLOCKED。
```

最低命令：

```powershell
git diff --check

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
npm run build
```

系统 smoke 顺序：

```text
1. 记录 commit、合入任务、OS、Go、CMake/Ninja/compiler、Node/npm、PostgreSQL、Redis、端口。
2. 清理旧进程、端口、PG fixture、Redis v2 测试 key 和临时目录。
3. 启动真实 PostgreSQL、Redis、app/admin。
4. 启动 app/agent。
5. 通过 app/web 或 Admin API 创建 / 启动 Runtime desired state。
6. 验证 PG desired/control_intents/audit。
7. 验证 Redis v2 projection 和 notify。
8. Agent 启动真实 navcaster-caster。
9. Caster health/metrics 可读。
10. Agent 上报 runtime-events / actual / metrics。
11. AdminService 聚合 desired + actual + lifecycle。
12. app/web 展示 desired / actual / events / metrics，并执行至少一次 action intent。
13. 单 Runtime NTRIP deterministic payload smoke。
14. 双 Runtime Redis Pub/Sub smoke，若本轮保留。
15. 清理并检查无残留进程、端口和测试 key。
```

通过标准：

```text
app/admin、app/agent、app/caster、app/web 全部来自 app layout。
真实闭环不使用 .archive。
真实闭环不使用旧 API/key/protobuf/Web 作为通过证据。
.archive 不参与默认 build/smoke/package/CI。
QA 记录写入 _team/qa/NC-113-v2-app-layout-integration-smoke.md。
```

## 10. L6 Review Gate

适用任务：

```text
NC-114
```

最低命令：

```powershell
git diff --stat team-dev..HEAD
git diff --check team-dev..HEAD
rg -n "add_subdirectory\\(.archive|\\.archive[/\\]v1[/\\](src|web)|npm --prefix web|web/dist|cd web|\\badmin\\b|\\bagent\\b|\\bcaster\\b|\\bsrc\\b" CMakeLists.txt deploy doc .github tools -g "*"
```

Reviewer 必查：

```text
根目录是否还残留生产态 admin/agent/caster/web/src。
app/admin、app/agent、app/caster、app/web 是否是 v2 唯一默认入口。
.archive/v1/src 和 .archive/v1/web 是否不参与默认构建、smoke、package、CI、QA。
CMake/Ninja 是否只默认构建 app/caster v2 runtime。
app/web 是否仍依赖旧 Web 页面、旧路由、旧 API fallback、mock-only login/session 或 demo adapter。
deploy/CI/package/QA/doc 路径是否同步。
NC-113 QA 证据是否覆盖 app layout 的系统闭环。
未运行项是否有原因、风险、后续任务和阻断判定。
```

Reviewer 结论：

```text
APPROVED
APPROVED_WITH_NOTED_RISKS
CHANGES_REQUESTED
BLOCKED
```

批准只能表示：

```text
可作为 v2 app layout baseline 继续演进。
不表示生产发布批准。
不表示 legacy v1 可以被删除出 Git 历史。
```

## 11. 路径扫描建议

路径扫描结果需要人工分类。以下命中不一定阻断：

```text
本文和 NC-108 文档中描述旧路径。
.archive 排除规则中的文字说明。
任务卡中描述迁移前路径。
显式 legacy-only 文档。
```

以下命中通常阻断：

```text
默认 build/smoke/package/CI 命令引用根 admin/agent/caster/web/src。
默认 build/smoke/package/CI 命令进入 .archive。
app/web 源码 import .archive/v1/web 或旧 pages/layouts。
CMake target 默认包含 .archive/v1/src。
package artifact 包含 .archive/v1/src 或 .archive/v1/web。
```

## 12. QA 记录模板

```markdown
# <Task ID> QA

## 结论

`QA_PASSED` / `QA_PASSED_WITH_NOTED_RISKS` / `QA_PARTIAL` / `QA_BLOCKED` / `QA_FAILED`

## 基线

- worktree:
- branch:
- commit:
- source baseline:
- date:
- merged task commits:

## 环境

- OS:
- Go:
- CMake / Ninja / compiler:
- Node / npm:
- PostgreSQL:
- Redis:
- browser:

## 验证范围

- 覆盖的 app layout gate:
- 明确排除的 legacy 证据:
- 未覆盖:

## 命令结果

| 命令 | 结果 | 证据 / 说明 |
| --- | --- | --- |
| `git diff --check` | PASS/FAIL |  |
| `cd app/admin; go test ./...` | PASS/FAIL/NR |  |
| `cd app/agent; go test ./...` | PASS/FAIL/NR |  |
| `build_ninja.ps1 -Target navcaster-caster` | PASS/FAIL/NR |  |
| `navcaster-caster --self-test` | PASS/FAIL/NR |  |
| `cd app/web; npm run build` | PASS/FAIL/NR |  |

## 路径隔离检查

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| 根生产态 admin/agent/caster/web/src 不存在 | PASS/FAIL/NR |  |
| .archive 不参与默认 CMake | PASS/FAIL/NR |  |
| .archive 不参与默认 Web build | PASS/FAIL/NR |  |
| .archive 不进入 package / CI | PASS/FAIL/NR |  |
| app/web 不含旧页面 / 旧 route / 旧 API fallback 默认依赖 | PASS/FAIL/NR |  |

## 系统 Smoke

| 步骤 | 结果 | 证据 |
| --- | --- | --- |
| Admin + PG + Redis health | PASS/FAIL/NR |  |
| Agent register / heartbeat | PASS/FAIL/NR |  |
| Web or API creates Runtime desired | PASS/FAIL/NR |  |
| PG desired / control_intents / audit | PASS/FAIL/NR |  |
| Redis v2 projection / notify | PASS/FAIL/NR |  |
| Agent starts real Caster | PASS/FAIL/NR |  |
| Caster health / metrics | PASS/FAIL/NR |  |
| runtime-events / actual / metrics ingest | PASS/FAIL/NR |  |
| app/web browser convergence | PASS/FAIL/NR |  |
| Single Runtime NTRIP | PASS/FAIL/NR |  |
| Dual Runtime Redis Pub/Sub | PASS/FAIL/NR |  |

## 未运行项

| 项目 | 原因 | 风险 | 后续任务 | 是否阻断当前 gate |
| --- | --- | --- | --- | --- |

## 阻断项

- 无 / 列表

## 备注

- 只接受 app/* v2 契约证据；.archive、旧 API/key/protobuf/Web 不计入通过标准。
```
