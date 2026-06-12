# NavCaster 迭代进展备忘录

生成时间：2026-06-12

这个文件用于后续长期协作。每次推进架构、Redis schema、功能迭代或重要修复时，都要更新这里。

## 当前目标

先完成架构和 Redis 数据模型设计，再按阶段渐进式重构。

## 当前状态

- 仓库已重新 clone 到 `F:\Dev\NavCaster`。
- 当前代码快照：`main` @ `96dd728`。
- 已生成项目记忆文件：`doc/project-memory.md`。
- 已完成 Redis/账号/模块边界初步评估。
- 已确认第一轮迭代重点：Redis schema 与账号模型。

## 设计文档

- [x] `doc/project-memory.md`：项目整体速记。
- [x] `doc/architecture-v2.md`：目标架构和模块边界。
- [x] `doc/redis-schema-v2.md`：目标 Redis schema。
- [x] `doc/refactor-plan-v2.md`：阶段化重构路线。
- [x] `doc/iteration-progress.md`：本备忘录。

## 关键发现

- Redis 当前同时承担运行时状态、持久配置、账号鉴权、Pub/Sub、历史监控。
- 继续使用 Redis 是合理的，但必须收敛 key/schema 管理。
- 当前 proto 的实际使用方式是“proto 定义结构，Redis 存 proto JSON”，不是二进制 protobuf。
- `ACT:RECORD` 是 HTTP 账号 CRUD 主表。
- NTRIP 鉴权实际读取 `ACT:ACTIVE`。
- HTTP 活跃账号 API 当前读取 `STR:ACTIVE`。
- `ACT:RECORD`、`ACT:ACTIVE`、`STR:ACTIVE` 的语义和同步关系需要第一优先级整理。
- 当前密码字段是明文/兼容明文路径，需要设计哈希迁移。
- `http_handler.cpp` 和 `caster_internal` 都偏重，需要后续拆分。

## 当前计划

### Phase 0：设计基线

- [x] 梳理项目结构。
- [x] 评估 Redis 与 proto 方案。
- [x] 生成目标架构文档。
- [x] 生成 Redis schema 文档。
- [x] 生成重构计划和进展备忘录。

### Phase 1：Redis Schema 与账号模型

- [x] 新增 Redis key registry。
- [x] 新增账号 schema helper。
- [x] 明确 `ACT:RECORD`、`ACT:ACTIVE`、`ACT:REC:*`、`ACT:UND:*`、`STR:ACTIVE` 的目标语义。
- [x] 增加账号同步函数。
- [x] HTTP 账号 CRUD 同步登录索引。
- [x] Auth 鉴权兼容新旧账号字段。
- [x] 引入密码哈希字段并兼容明文迁移。
- [x] 添加最小验证脚本或 smoke test。

### Phase 2：Repository 层

- [ ] 抽账号 repository。
- [ ] 抽 source/access/relay/runtime repository。
- [ ] 统一 proto JSON helper。
- [ ] HTTP CRUD 改为调用 repository。

### Phase 3：拆 HTTP handler

- [ ] 按业务域拆 controller。
- [ ] SSE 数据源改为 service/repository。

### Phase 4：拆 Caster Core

- [ ] 抽 AccessPolicy、SourceTable、Cluster、RelayScheduler、History 等服务。
- [ ] 保留 `CASTER::*` facade。

### Phase 5：物理目录迁移

- [ ] 等边界稳定后再移动目录和 CMake。

## 下一步建议

从 Phase 1 开始：

1. 新增 Redis key registry 和账号 schema helper。
2. 梳理并实现 `ACT:RECORD -> ACT:ACTIVE` 同步。
3. 设计 `STR:ACTIVE` 到 `ACT:SESSION` 或 `ACT:REC:*` 的迁移兼容。
4. 为实名账号登录路径补验证。

## 待确认问题

- 是否要保持 Redis 双实例：Caster Redis 与 Auth Redis。
- `STR:ACTIVE` 是否有外部系统依赖。
- 管理台账号密码是否需要立即强制哈希迁移。
- 后续是否需要多管理员和角色权限。
- 长期历史是否继续放 Redis，还是未来迁移到数据库。

## 工作记录

### 2026-06-12

- 重新 clone 项目并确认仓库干净。
- 生成 `doc/project-memory.md`。
- 评估 Redis 数据结构、proto JSON、用户管理是否需要新数据库。
- 决定短期保留 Redis，先统一 schema 和账号模型。
- 生成架构与重构设计文档。
- 提交文档基线：`docs: add architecture and iteration baseline`。
- 新增 `src/core/context/redis_keys.*`，集中登记 Redis key 和动态 key 拼接。
- 新增 `src/core/context/account_schema.*`，提供账号记录默认值、登录启用判断、活动索引构建、legacy 明文密码视图。
- 新增 `tools/schema_smoke` 轻量测试例程，验证 Redis key helper 和账号 schema helper。
- 扩展 `account_schema` 同步计划：生成 `ACT:RECORD -> ACT:ACTIVE` 的写入/删除决策，并定义账号删除时同步清理主表与登录索引。
- 扩展 `schema_smoke`：覆盖启用账号写 active index、过期/冻结账号删除 active index、缺失账号拒绝、账号删除计划、legacy 字段兼容、hash 密码优先级、Redis key 语义防回退。
- HTTP 账号创建/更新/删除接入同步计划：写入规范化 `ACT:RECORD`，按账号状态同步写入或清理 `ACT:ACTIVE` 登录索引，删除账号时同步清理登录索引；更新时拒绝 URL 与 body account 不一致并保留原 `create_time`。
- Auth 登录读取接入 `account_schema`：`auth_limit` 兼容 `connection_limit/connect_limit`、`group_uid/group`、`expire_time/expire` 和布尔/数字 `active`；登录密码比较改用 `password_matches`，存在未知 `password_hash` 时 fail closed，legacy 明文仍兼容。
- 定稿账号相关 Redis key 语义：`ACT:RECORD` 为主表，`ACT:ACTIVE` 为登录索引，`ACT:REC:*`/`ACT:UND:*` 为在线连接桶，`STR:ACTIVE` 标记为 legacy 在线展示表。
- 引入 `pbkdf2-sha256` 密码哈希：HTTP 新建/更新账号时将 legacy `password` 迁移为 `password_hash/password_algo/password_salt/password_iterations`，`ACT:RECORD` 与 `ACT:ACTIVE` 不再新增明文密码；Auth 登录支持 PBKDF2 校验，legacy 明文仅作为旧数据兼容路径，未知 hash/缺少 salt/非法迭代次数 fail closed。
- HTTP 更新账号时保留既有密码材料：普通资料更新或空 `password` 不会清掉旧 hash/legacy 密码；显式提交新密码时重新生成 hash。
- 扩展 `schema_smoke`：覆盖 PBKDF2-SHA256 标准向量、新写账号移除明文、active index hash 字段完整性、hash 登录成功/失败、legacy 明文兼容、hash 优先防明文绕过、更新保留密码材料、新密码轮换、缺密码/无效 hash 拒绝。
- 验证结果：
  - `cmake -S . -B build` 通过，存在全局 git ignore 权限和 libevent dubious ownership 环境警告。
  - `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - 扩展同步计划后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - 扩展同步计划后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - HTTP 双写改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - HTTP 双写改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Auth 兼容改动后执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Auth 兼容改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Auth 兼容改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - 密码哈希迁移改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - 密码哈希迁移改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - 密码哈希迁移改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - `cmake --build build --target casterhttp --config Release --parallel` 在 Windows/MSVC 环境被既有跨平台头文件问题阻塞：先后失败于 `src/core/src/Caster_Core.cpp`/`caster_internal.cpp` 的 `unistd.h`，以及窄构建 `http_handler.cpp` 的 `sys/socket.h`。本轮未将 `casterhttp` 作为通过依据。
  - `cmake --build build --target castercore --config Release --parallel` 超过 120 秒未完成，本轮未作为通过依据；已终止该次超时遗留的构建进程树。
