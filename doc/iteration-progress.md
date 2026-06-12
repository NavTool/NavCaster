# NavCaster 迭代进展备忘录

生成时间：2026-06-12

这个文件用于后续长期协作。每次推进架构、Redis schema、功能迭代或重要修复时，都要更新这里。

## 当前目标

先完成架构和 Redis 数据模型设计，再按阶段渐进式重构。

## 当前状态

- 仓库已重新 clone 到 `F:\Dev\NavCaster`。
- 当前代码快照：`main` @ `96dd728`。
- 当前迭代分支：`codex-redis-schema-iteration`，已持续按小功能提交推进。
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

- [x] 抽账号 repository。
- [x] 抽 source/alias repository。
- [x] 抽 access repository。
- [x] 抽 relay repository。
- [x] 抽 runtime state repository 边界。
- [x] 统一 proto JSON helper。
- [x] HTTP CRUD 全部改为调用 repository。

### Phase 3：拆 HTTP handler

- [~] 按业务域拆 controller。
- [x] SSE 数据源改为 service/repository。
- [x] Sourcetable 工具接口拆为 service，并补跨平台 TCP 封装。

### Phase 4：拆 Caster Core

- [ ] 抽 AccessPolicy、SourceTable、Cluster、RelayScheduler、History 等服务。
- [ ] 保留 `CASTER::*` facade。

### Phase 5：物理目录迁移

- [ ] 等边界稳定后再移动目录和 CMake。

## 下一步建议

继续 Phase 3/4：

1. 统计接口：先抽纯聚合函数和 service 测试，再接入 `/api/stats/*` handler。
2. 历史接口：拆 `LOG:MPT:*`、`LOG:USR:*`、`NODE:HISTORY:*` 的 repository/service 边界。
3. 运维接口：拆 audit、ring logs、monitor Redis/cluster、node log level。
4. Caster Core：开始抽 SourceTable、AccessPolicy、RelayScheduler、History 等服务，保留 `CASTER::*` facade。

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
- 新增 `RedisHashClient` 和 `AccountRepository`：账号列表/详情/legacy active sessions/create/update/delete 收敛到 repository，HTTP 账号 CRUD 只做请求解析和 result -> HTTP status 映射。
- HTTP 账号 CRUD 替换为调用 `AccountRepository`，保持 400/404/409/500 行为，并把 `ACT:RECORD -> ACT:ACTIVE` 同步与密码材料保留逻辑从 handler 中移出。
- 扩展 `schema_smoke` fake Redis 测试：覆盖 repository create 双写主表与登录索引、重复创建冲突、禁用更新清理登录索引、更新保留密码材料、URL/body account 冲突、缺失更新、删除同步清理主表和登录索引。
- 新增 `RepositoryStatus` 公共状态、`SourceRepository` 和 `AliasRepository`：source/alias 列表、详情、create/update/delete 收敛到 repository，HTTP source/alias CRUD 改为调用 repository。
- Source 写入计划补齐 `uid=mountpoint`、`source_group_uid=default`、`record_type/decode_type/display_type`、`create_time/update_time`，并拒绝 update URL 与 body `mountpoint` 不一致。
- Alias 写入计划兼容 `uid/alias_name/alias_mpt/name`，补齐 `uid/alias_name/source_name/enable/visible/create_time/update_time`；create/update/delete 成功后继续发布 `CASTER:CONF` 的 `ALIAS` 变更通知。
- 扩展 `schema_smoke`：覆盖 source 计划默认值、缺 mountpoint 拒绝、update mountpoint 冲突、source repository CRUD；覆盖 alias key fallback、缺 source_name 拒绝、update 使用 URL uid、alias repository CRUD 和 publish 行为。
- 新增 `AccessRepository`：内置 `default`/`SYSTEM` 组初始化、access group CRUD、`ACCESS:ITEM:<group_uid>` CRUD 收敛到 repository；HTTP access group/item handler 改为调用 repository。
- Access group 写入计划兼容 `uid/group_uid`，补齐 `group_name/create_time/update_time` 和六个访问控制默认开关；内置组删除仍在 HTTP 层保持 403 行为。
- Access item 写入计划兼容 `mount_point_name/mountpoint/mount/uid`，统一使用 `redis_keys::access_item(group_uid)` 分桶；group/item 写成功后继续发布 `CASTER:CONF` 的 `ACCESS` 变更通知，启动内置组初始化不发布。
- 扩展 `schema_smoke`：覆盖内置组初始化、group 默认值/重复创建/更新/内置组保护、item mountpoint alias 兼容、item CRUD、`ACCESS` publish 行为。
- 新增 `RelayRepository`：pull/push record、state 读取、create/update/delete/start/stop 收敛到 repository，HTTP pull/push relay handler 改为调用 repository。
- Relay 写入计划保持 `uid` 必填和 `enabled=true` 默认；update/delete 会清理对应 `PULL:STAT`/`PUSH:STAT` 以触发调度刷新，start/stop 只更新 `enabled` 且不删除 state，保留原来的 INACTIVE 广播保护语义。
- 扩展 `schema_smoke`：覆盖 pull/push key 选择、create 默认 enabled、重复创建冲突、update/delete 清 state、start/stop 不清 state、push 显式 enabled=false 保留。
- 新增 `RuntimeStateRepository`：server/client/stream/node 只读运行态 hash 的 list/get 边界收敛到 repository。
- HTTP server/client/stream/node 列表与详情 handler 改为调用 `RuntimeStateRepository`；kick、monitor、SSE、history、statistics 等行为型路径保持现状，留到 Phase 3/4 按 service 边界继续拆分。
- 扩展 `schema_smoke`：覆盖 runtime state key 映射、server 状态 list/get、空 ID 详情查询返回空。
- 新增 `ConfigRepository`：`CONF:SERVICE`、`CONF:CORE`、`CONF:AUTH` 的 list/get/update/save 边界收敛到 repository，HTTP config handler 与运行时登录读取 auth config 改为调用 repository。
- 扩展 `RedisHashClient` 的 `get/set` 字符串 key 接口，保持 hash repository 现有行为不变；`schema_smoke` fake Redis 同步支持配置读写测试。
- 扩展 `schema_smoke`：覆盖 config section 解析、CONF key 映射、配置更新/读取/list、raw JSON save、Redis set 失败映射为 `RedisError`。
- 新增 `json_record` helper：统一 record JSON 的 dump/parse/coerce、数字/布尔转换、字符串字段读取和 `create_time/update_time` 维护；保持现有 protobuf JSON 选项不变，避免改变 Redis/API 格式。
- 账号/source/alias/access/config/relay repository 改为使用 `json_record` 的机械 JSON 边界 helper；`RelayRepository::set_enabled` 通过 `coerce_record` 兼容 Redis 中的 object 或 stringified object。
- 清理 `http_handler.cpp` 中已无调用的 `IMPL_*` CRUD 宏和 header 中旧的 `DeferredResponse` 死代码；Phase 2 管理型 HTTP CRUD/配置写入口均已收敛到 repository，SSE/monitor/history/statistics/kick 等行为型直连留给 Phase 3/4。
- 扩展 `schema_smoke`：覆盖 `json_record` timestamp、数字/布尔、字符串字段、dump/parse/coerce、非法 JSON 与非 object 拒绝行为。
- 新增 Phase 3 controller 样板：`ConfigController` 与 `ControllerResponse`，将 `/api/config` list/get/update/save 业务逻辑从 `http_handler.cpp` 移出，`http_handler` 仅保留路由委托和响应转接。
- `ConfigController` 保持 auth 配置语义：GET auth 只返回 `admin_user` 不暴露密码；PUT auth 需要 `old_password`，校验后保存时剥离 `old_password`。
- 扩展 `schema_smoke`：直接编译并测试 `ConfigController`，覆盖默认 auth 用户、隐藏密码、未知 section、service 更新、auth old password 400/403/成功路径、非法 JSON。
- 新增 `SourceController`：将 `/api/sources` list/get/create/update/delete 业务逻辑从 `http_handler.cpp` 移出，`http_handler` 保留轻量委托；controller 通过 `SourceRepository` 和注入时间戳保持原有 HTTP 状态码与响应体。
- 扩展 `schema_smoke`：直接编译并测试 `SourceController`，覆盖 source list/create/get/update/delete、重复创建 409、非法 JSON 400、缺 ID 400、缺失资源 404。
- 新增 `SseSnapshotService`：SSE channel 快照统一改为通过 RuntimeState/Account/Source/Alias/Access/Relay repository 获取，`sse_manager` 继续只负责连接、轮询和广播。
- 修正 SSE `accounts` 与 `account_actives` 数据源：从 caster Redis 直连切换为 auth Redis repository，和 REST 账号 API 的 Redis 实例保持一致；其余 caster 运行态/配置/relay channel 通过 caster Redis repository。
- 扩展 `schema_smoke`：覆盖所有 SSE snapshot channel 的 repository 映射，并断言 `accounts` 忽略 caster Redis 中的 `ACT:RECORD`、读取 auth Redis 中的账号数据。
- 新增 `ControllerResponse`/`controller_helpers` 公共 HTTP controller helper，复用 JSON 响应、错误响应、JSON body parse 与 `RepositoryStatus -> HTTP status` 映射。
- 新增 `AliasController`：将 `/api/aliases` list/get/create/update/delete 业务逻辑从 `http_handler.cpp` 移出，保留 `AliasRepository` 的 `CASTER:CONF ALIAS` publish 副作用。
- 扩展 `schema_smoke`：直接编译并测试 `AliasController`，覆盖 alias list/create/get/update/delete、publish、重复创建 409、非法 JSON 400、缺 source_name 400、缺 ID 400、缺失资源 404。
- 新增 `AccessController`：将 `/api/access/groups` 与 `/api/access/items/<group_uid>` 的 list/get/create/update/delete 业务逻辑从 `http_handler.cpp` 移出，保持 access group 内置组删除 403、item `mount_point_name/mountpoint/mount/uid` 兼容和 `CASTER:CONF ACCESS` publish 副作用。
- 扩展 `schema_smoke`：直接编译并测试 `AccessController`，覆盖 group CRUD、内置组保护、重复创建 409、非法 JSON 400、缺 ID/缺 group_uid、item mount 字段兼容、body/path group_uid fallback、item CRUD 和 publish 行为。
- 新增 `RelayController`：将 `/api/relays/pull`、`/api/relays/push`、relay state list 以及 start/stop 业务逻辑从 `http_handler.cpp` 移出；路由处显式传入 `RelayKind`，避免 start/stop 继续依赖 path 字符串推断 pull/push。
- 扩展 `schema_smoke`：直接编译并测试 `RelayController`，覆盖 pull/push list/get/create/update/delete/status、重复创建 409、非法 JSON 400、缺 ID 400、缺失记录 404、start/stop 只更新 `enabled` 且不清理 state、update/delete 清理 state。
- 新增 `AccountController`：将 `/api/accounts`、`/api/accounts/*` 和 `/api/accounts/active` 业务逻辑从 `http_handler.cpp` 移出，继续使用 auth Redis，并保留 `active` 特殊查询读取 legacy `STR:ACTIVE` 的行为。
- 扩展 `schema_smoke`：直接编译并测试 `AccountController`，覆盖账号 list/get/create/update/delete、active 特殊查询、重复创建 409、非法 JSON 400、缺账号名 400、缺失账号 404、URL/body account 冲突、禁用账号清理 `ACT:ACTIVE`、空密码更新保留 hash、新密码轮换。
- 新增 `RuntimeStateController`：将 servers/clients/streams/nodes 四类运行态 list/get 只读逻辑从 `http_handler.cpp` 移出，kick、node history、statistics 等行为型路径暂留后续 service/controller 拆分。
- 扩展 `schema_smoke`：直接编译并测试 `RuntimeStateController`，覆盖 Server/Client/Stream/Node 四类 key 的 list/get、空 ID 400、缺失记录 404。
- 新增 `RuntimeCommandService`：将 server/client kick 的在线状态检查和 `CASTER:BROADCAST` delete 广播发布逻辑从 `http_handler.cpp` 移出，handler 仅负责按路由传入 Server/Client 类型和 uid。
- 扩展 `schema_smoke`：直接编译并测试 `RuntimeCommandService`，覆盖缺 uid 400、非 server/client 目标 400、离线对象 404、publish 失败 500、server/client kick 成功，并解析真实 `broadcast_msg` JSON 校验广播类型、delete 操作和 target。
- 新增 `SourcetableService`：将 `/api/utils/sourcetable` 远端抓取和 `/api/utils/sourcetable/local` 本地源表解析从 `http_handler.cpp` 移出，提供可注入 fetcher、NTRIP 1.0/2.0 请求构造、STR 行解析和统一 HTTP 响应。
- Sourcetable TCP 抓取补跨平台封装：Windows 使用 Winsock/`ws2_32`，POSIX 继续使用 socket API；`http_handler.cpp` 不再直接包含 `sys/socket.h`/`netdb.h`/`unistd.h`。
- 顺手补齐 `http_handler.cpp` 统计路径的日期解析/本地时间跨平台 helper，用 `std::get_time` 与 `localtime_s/localtime_r` 包装替代 MSVC 不支持的 `strptime/localtime_r` 直调，解除窄构建 `casterhttp` 的 Windows 编译阻塞。
- 扩展 `schema_smoke`：覆盖 sourcetable STR 解析、`ENDSOURCETABLE` 截止、NTRIP 1.0/2.0 请求头、Basic Auth、远端 mock fetch 成功/失败、非法 JSON、缺 host 和本地源表解析。
- 新增 `StatisticsService`：将 `/api/stats/overview`、`/api/stats/daily/*`、mountpoint/user ranking 的纯聚合逻辑从 `http_handler.cpp` 移出；handler 仍保留 query 参数解析、Redis 读取和 daily cache 行为。
- Statistics 聚合 service 保持原有语义：PULL/PUSH 单独计数，普通 mpt/usr 计算平均时长和唯一对象，趋势桶按会话覆盖区间累加，overview 继续自适应 1h/1d/1w 桶并最多 200 桶，daily 固定 24 个小时桶。
- 扩展 `schema_smoke`：固定样本覆盖 overview/daily 计数、峰值并发、平均时长、唯一 mount/user、趋势桶、ranking 排序、limit、last_seen、mount_count 和类型标签。
- 新增 `ConnectionHistoryRepository` 与 `ConnectionHistoryService`：将 `/api/logs/servers`、`/api/logs/clients` 的 `LOG:MPT:*`/`LOG:USR:*` prefix 聚合读取从 `http_handler.cpp` 移出，保持原有跨 hash field 聚合响应形状。
- `RedisHashClient` 增加默认 `scan_hgetall_prefix` 扩展点，真实 `sync_redis` 继续使用既有实现，`schema_smoke` fake Redis 覆盖该接口以支持后续 history/monitor 拆分测试。
- 扩展 `schema_smoke`：覆盖 connection history key prefix、server/client prefix scan 聚合隔离、repository list 和 service 200 响应。
- 扩展 `ConnectionHistoryService`：将 `/api/stats/mountpoints/history/*` 和 `/api/stats/users/history/*` 的单对象明细读取、`duration/online` 计算、`connect_time` 降序排序从 `http_handler.cpp` 移出；该路由虽然挂在 stats 下，但边界归入 history。
- 扩展 `schema_smoke`：覆盖 connection history detail 的非 object 忽略、在线/离线 duration、降序排序、client detail、缺 mount/user 400。
- 新增 `NodeHistoryRepository` 与 `NodeHistoryService`：将 `/api/nodes/history/*` 的 raw/1m/5m key 选择、limit 默认值和上限、`LRANGE` 读取从 `http_handler.cpp` 移出。
- `RedisHashClient` 增加默认 `lrange` 扩展点，`schema_smoke` fake Redis 支持 list 数据，为 node/system/audit/monitor history 后续拆分复用。
- 扩展 `schema_smoke`：覆盖 node history raw/1m/5m key、range 解析 fallback、limit 默认与 5m cap、repository 顺序/limit、service range 选择和缺 node 400。
- 新增 `SystemEventRepository` 与 `SystemEventService`：将 `/api/system/events` 从 `http_handler.cpp` 移出，并修正读取路径为扫描 `LOG:NODE:*` hash 后聚合事件，匹配 core 写入端和 Redis schema 文档。
- `RedisHashClient` 增加默认 `scan_all_keys` 扩展点，`schema_smoke` fake Redis 支持 key 扫描；system events service 继续保留 limit 默认/上限和 timestamp 降序。
- 扩展 `schema_smoke`：覆盖 `LOG:NODE:<id>` hash 事件聚合、非 node key 忽略、非 object 忽略、timestamp 降序、limit 截断与非法 limit fallback。
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
  - 账号 repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - 账号 repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - 账号 repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - 账号 repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`，未暴露新增 repository 编译错误。
  - Source/Alias repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Source/Alias repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Source/Alias repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Source/Alias repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - Access repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Access repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Access repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Access repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - Relay repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Relay repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Relay repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Relay repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - Runtime state repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Runtime state repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Runtime state repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Runtime state repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - Config repository 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Config repository 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Config repository 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Config repository 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - JSON record helper 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - JSON record helper 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - JSON record helper 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - JSON record helper 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`。
  - Config controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Config controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Config controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Config controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `config_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Source controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Source controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Source controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Source controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `source_controller.cpp` 已被 `casterhttp` 目标收编译。
  - SSE snapshot service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - SSE snapshot service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - SSE snapshot service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - SSE snapshot service 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `sse_snapshot_service.cpp` 已被 `casterhttp` 目标收编译。
  - Alias controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Alias controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Alias controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Alias controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `alias_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Access controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Access controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Access controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Access controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `access_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Relay controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Relay controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Relay controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Relay controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `relay_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Account controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Account controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Account controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Account controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `account_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Runtime state controller 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Runtime state controller 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Runtime state controller 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Runtime state controller 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `runtime_state_controller.cpp` 已被 `casterhttp` 目标收编译。
  - Runtime command service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Runtime command service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Runtime command service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Runtime command service 改动后执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false`，仍被既有 Windows/MSVC 头文件问题阻塞于 `src/http/http_handler.cpp` 的 `sys/socket.h`；日志显示 `runtime_command_service.cpp` 已被 `casterhttp` 目标收编译。
  - Sourcetable service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Sourcetable service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Sourcetable service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Sourcetable service 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过，`http_handler.cpp` 的 `sys/socket.h` 和 `strptime/localtime_r` Windows/MSVC 阻塞已解除。
  - Statistics service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Statistics service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Statistics service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Statistics service 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Connection history read 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Connection history read 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Connection history read 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Connection history read 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Connection history detail 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Connection history detail 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Connection history detail 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Connection history detail 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Node history service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - Node history service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - Node history service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - Node history service 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - System event service 改动后重新执行 `cmake --build build --target schema_smoke --config Release --parallel` 通过。
  - System event service 改动后重新执行 `bin\Release\schema_smoke.exe` 通过，输出 `[schema_smoke] all checks passed`。
  - System event service 改动后重新执行 `cmake --build build --target authverify --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - System event service 改动后重新执行 `cmake --build build --target casterhttp --config Release --parallel -- /p:BuildProjectReferences=false` 通过。
  - `cmake --build build --target casterhttp --config Release --parallel` 在 Windows/MSVC 环境的完整依赖构建仍可能被 `src/core/src/Caster_Core.cpp`/`caster_internal.cpp` 的 `unistd.h` 阻塞；本轮以窄构建 `casterhttp -- /p:BuildProjectReferences=false` 验证 HTTP 目标自身编译通过。
  - `cmake --build build --target castercore --config Release --parallel` 超过 120 秒未完成，本轮未作为通过依据；已终止该次超时遗留的构建进程树。
