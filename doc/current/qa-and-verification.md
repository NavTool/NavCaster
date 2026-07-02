# QA 与验证体系当前说明

更新时间：2026-06-16
来源：NC-035 QA 文档审计、NC-034 Ninja 构建任务和 `qa/qa-gates.md`。

## 当前默认构建口径

C++ 本地构建默认使用 CMake + Ninja + 全处理器并行：

```powershell
.\deploy\scripts\admission_check.ps1 -BuildType Release
.\deploy\scripts\build_ninja.ps1 -BuildType Release -ConfigureOnly
.\deploy\scripts\build_ninja.ps1 -BuildType Release
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target schema_smoke
```

Linux：

```bash
BUILD_TYPE=Release bash deploy/scripts/build_ninja.sh
BUILD_TYPE=Release bash deploy/scripts/build_ninja.sh --target schema_smoke
```

`schema_smoke` 已注册为 CTest：

```powershell
ctest --test-dir build\ninja-Release --output-on-failure -R schema_smoke
```

Windows 基线配置不要在普通 PowerShell 中直接依赖裸 `cmake --preset ninja-release`。
如果 PATH 上先命中不可执行的 WinGet `ninja.exe` shim，CMake 会在启用 C/C++
语言前失败。`build_ninja.ps1 -ConfigureOnly` 是团队准入入口；裸 preset 只适用于
已经进入 Visual Studio developer shell 或已手工设置真实 Ninja 与编译器 PATH 的环境。

## 契约检查

```powershell
node tools\contract_check\check_api_contracts.mjs
```

该命令比较 `proto/caster` 中关键 message/enum 与归档 v1 类型
`.archive/v1/web/src/api/types.ts` 的同步状态。新增允许差异必须同步脚本 allowlist
和 `api/api-contract-sync.md`。v2-only 控制台位于 `app/web`，不再维护旧 proto mirror。

## Web 验证

```bash
cd app/web
npm ci
npm run lint
npm run build
```

当前 `npm run build` 是常规门槛。`npm run lint` 仍有既有基线债，前端任务必须运行并记录结果，但主 CI 还不能把 lint 当硬门槛。

`deploy/scripts/admission_check.ps1` 是 Windows 本地合并准入聚合入口。它把 API
contract check、Ninja configure、`schema_smoke` 构建/执行、CTest 和 Web
production build 作为强阻断项；lint 和 e2e matrix 保持报告项或任务触发项。

## 运行态 smoke

Windows 深度 smoke 主入口：

```powershell
.\deploy\scripts\e2e_smoke.ps1 -RedisMode Docker -Configuration Release
```

可选专项包括 active accounts、SSE delta、NTRIP/Auth、anonymous/auth broadcast、disabled account、Redis reconnect、local dual node、relay pull/push、relay data forwarding、relay push failover、Docker bridge cluster、HTTP ingress strategy、master lease 和 relay failover。

Bash HTTP smoke 用于已有服务：

```bash
BASE=http://127.0.0.1:8080 USER=admin PASS=admin bash deploy/scripts/e2e_smoke.sh
```

## v2 AdminService 验证

`app/admin/` 是 Go AdminService，不走 CMake target。NC-091 起最低本地验证：

```powershell
cd app\admin
gofmt -w ./...
go test ./...
go build ./cmd/navcaster-admin
```

最小 API self-check：

```powershell
cd app\admin
go run ./cmd/navcaster-admin
# 另一个终端：
go run ./cmd/navcaster-admin-selfcheck
```

配置 `NAVCASTER_ADMIN_POSTGRES_DSN` 后 self-check 覆盖 PostgreSQL source-of-truth 和
migration 路径；配置 `NAVCASTER_ADMIN_REDIS_ADDR` 后还覆盖 Redis v2 projection 写入。
无 PG/Redis 环境时允许使用内存仓储降级 self-check，但 QA 记录必须明确真实 PG/Redis 未运行原因。

## 文档治理任务 QA 口径

NC-035 这类文档治理任务不开发新功能、不修改产品源码逻辑、不新增 e2e 场景。最低检查：

```powershell
git status --short
git diff --check
git diff --name-status team-dev...HEAD
git diff --stat team-dev...HEAD
rg --files doc
Test-Path docs
rg -n "repo\\docs|repo/docs|docs\\" doc F:\Projects\NavCaster\_team F:\Projects\NavCaster\shared
```

通过条件：

- `doc` 是唯一仓库文档入口；
- `docs` 不存在；
- `doc/README.md` 中列出的路径存在；
- 历史、协议、草稿资料有明确分类和说明；
- diff 不包含产品源码逻辑改动；
- 未运行 C++/Web/e2e 的原因在 QA 记录中说明。
