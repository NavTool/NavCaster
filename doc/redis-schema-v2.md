# NavCaster Redis Schema V2

生成时间：2026-06-12

本文档定义目标 Redis 数据模型。当前代码中已有部分 key 与本文档不完全一致，后续迭代以本文档为目标逐步迁移。

## 设计原则

- Redis 分四类数据：运行时状态、持久配置、账号鉴权、历史监控。
- 运行时状态必须有 TTL 或明确清理策略。
- 持久配置必须有 schema、默认值和迁移策略。
- 所有 key、field、value schema 统一登记，不在业务代码里散落硬编码。
- value 默认使用 proto JSON。少量历史日志可使用普通 JSON，但字段必须文档化。
- Pub/Sub 频道和数据存储 key 分开命名和管理。

## 命名约定

- `DOMAIN:KIND`：全局 hash/string/list。
- `DOMAIN:KIND:<id>`：按实体分桶。
- `NODE:<node_id>`：节点私有控制频道。
- `MPT:<mount>`、`USR:<user>`：二进制实时数据频道，保留现状。
- 历史数据使用分桶 key，避免单个大 hash。

## 数据分类

### 运行时状态

这类数据描述当前连接、节点、任务运行状态。应自动过期。

| Key | Type | Field | Value | 生命周期 | 写入者 | 读取者 |
| --- | --- | --- | --- | --- | --- | --- |
| `CASTER:MASTER` | STRING | - | node_id | `EX` 主节点续期 | ClusterService | ClusterService/HTTP |
| `CASTER:NODE` | HASH | node_id | `CasterNode` proto JSON | `HSETEX` | ClusterService | HTTP/SSE/RelayScheduler |
| `MPT:LIST` | HASH | mount | timestamp/source info | `HSETEX` | CasterService | SourceTable/HTTP/Core |
| `USR:LIST` | HASH | user | timestamp/basic info | `HSETEX` | CasterService | Core/HTTP |
| `MPT:STAT` | HASH | connect_key | `ServerState` proto JSON | `HSETEX` | CasterService | HTTP/SSE |
| `USR:STAT` | HASH | connect_key | `ClientState` proto JSON | `HSETEX` | CasterService | HTTP/SSE |
| `STR:STAT` | HASH | connect_key | `StreamState` proto JSON | `HSETEX` | CasterService | HTTP/SSE/Stats |
| `PULL:STAT` | HASH | task_uid | `PullState` proto JSON | `HSETEX` | Relay session | HTTP/SSE/RelayScheduler |
| `PUSH:STAT` | HASH | task_uid | `PushState` proto JSON | `HSETEX` | Relay session | HTTP/SSE/RelayScheduler |
| `MPT:REC:<mount>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | CasterService |
| `USR:REC:<user>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService/AuthService | CasterService/AuthService |
| `MPT:SUB:<mount>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | HTTP/Core |
| `USR:SUB:<user>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | Core |
| `MPT:GEO` | GEO/ZSET | member=mount | lon/lat | 同在线状态清理 | CasterService | nearest |
| `USR:GEO` | GEO/ZSET | member=connect_key | lon/lat | 同在线状态清理 | CasterService | monitoring |

### 持久配置

这类数据由管理台配置，默认不过期。

| Key | Type | Field | Value | 生命周期 | 写入者 | 读取者 |
| --- | --- | --- | --- | --- | --- | --- |
| `MPT:RECORD` | HASH | mount | `SourceRecord` proto JSON | 持久 | HTTP/SourceRepository | SourceTable/Core |
| `MPT:SOURCE` | HASH | mount | `SourceRecord` proto JSON | `HSETEX` | CasterService 自动解码 | SourceTable/Core |
| `ALIAS:RULE` | HASH | uid | `AliasRule` proto JSON | 持久 | HTTP/AliasRepository | SourceTable/Core |
| `ACCESS:GROUP` | HASH | group_uid | `AccessGroup` proto JSON | 持久 | HTTP/AccessRepository | AccessPolicy |
| `ACCESS:ITEM:<group_uid>` | HASH | mount | `AccessItem` proto JSON | 持久 | HTTP/AccessRepository | AccessPolicy |
| `PULL:RECORD` | HASH | task_uid | `PullRecord` proto JSON | 持久 | HTTP/RelayRepository | RelayScheduler |
| `PUSH:RECORD` | HASH | task_uid | `PushRecord` proto JSON | 持久 | HTTP/RelayRepository | RelayScheduler |
| `CONF:SERVICE` | STRING or HASH | section | service config JSON/YAML | 持久 | HTTP/ConfigService | App/HTTP |
| `CONF:CORE` | STRING or HASH | section | core config JSON/YAML | 持久 | HTTP/ConfigService | Core |
| `CONF:AUTH` | STRING or HASH | section | auth config JSON/YAML | 持久 | HTTP/ConfigService | Auth/HTTP |

建议后续把 `CONF:*` 统一成 STRING JSON，避免当前文档与实现中 HASH/STRING 混用。

### 账号鉴权

目标语义：

| Key | Type | Field | Value | 生命周期 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `ACT:RECORD` | HASH | account | `AccountRecord` proto JSON | 持久 | 账号主表，Web CRUD 的唯一主数据 |
| `ACT:ACTIVE` | HASH | account | `AccountAuthIndex` 或兼容 `AccountRecord` JSON | 持久或按账号到期 TTL | NTRIP 登录快速校验索引 |
| `ACT:REC:<account>` | HASH | connect_key | timestamp/session JSON | 在线 TTL | 已登录实名账号连接 |
| `ACT:UND:<name>` | HASH | connect_key | timestamp/session JSON | 在线 TTL | 匿名登录连接 |
| `ACT:UNNAMED` | HASH | name | timestamp | 可选 TTL/清理 | 匿名账号痕迹 |
| `ACT:SESSION:<account>` | HASH | connect_key | `AccountActive` proto JSON | 在线 TTL | V2 建议新增，用于替代或补充 `STR:ACTIVE` |

本轮迭代定稿语义：

- `ACT:RECORD` 是账号主表，HTTP 账号 CRUD 只把它作为持久主数据。
- `ACT:ACTIVE` 是登录索引，只保存“当前允许登录”的账号派生视图；Auth 登录只读这个 key。
- `ACT:ACTIVE` 不表示在线会话，禁用、冻结、过期或删除账号时必须清理对应 field。
- `ACT:REC:<account>` 是实名账号在线连接桶，用于连接数限制和踢下线广播。
- `ACT:UND:<name>` 是匿名登录在线连接桶。
- `ACT:UNNAMED` 是匿名账号痕迹表，仅在匿名模式注册临时名称。
- `STR:ACTIVE` 是旧版在线账号展示表，当前 `/api/accounts/active` 仍兼容读取它；后续由 `ACT:SESSION:<account>` 或统一 session API 替换。

当前问题：

- HTTP 账号管理写 `ACT:RECORD`。
- NTRIP 鉴权读 `ACT:ACTIVE`。
- 活跃账号 API 读 `STR:ACTIVE`。
- `ACT:RECORD` 到 `ACT:ACTIVE` 没有清晰同步链路。
- `STR:ACTIVE` 没有在当前源码中发现明确写入点。

V2 建议：

- `ACT:RECORD` 是唯一账号主表。
- `ACT:ACTIVE` 是由 `ACT:RECORD` 派生的可登录索引。
- `ACT:SESSION:<account>` 或 `ACT:REC:<account>` 是在线会话。
- `STR:ACTIVE` 标记为 legacy，后续迁移到 `ACT:SESSION:*` 或重命名为 `ACT:SESSION`.

密码字段建议：

| 字段 | 说明 |
| --- | --- |
| `password_hash` | 新密码哈希，当前实现为 PBKDF2-SHA256 的 hex digest |
| `password_algo` | 当前支持 `pbkdf2-sha256`；未知算法登录 fail closed |
| `password_salt` | PBKDF2 salt |
| `password_iterations` | PBKDF2 迭代次数，默认 `100000` |
| `password` | legacy 明文字段，仅迁移期读取，不再写入 |

迁移规则：

- HTTP 新建或修改账号时，如果收到 legacy `password`，写入前自动转换为 `password_hash/password_algo/password_salt/password_iterations`，并从 `ACT:RECORD` 与 `ACT:ACTIVE` 派生视图中移除明文 `password`。
- HTTP 修改账号时，如果请求未携带新密码或仅携带空 `password`，沿用当前 Redis 记录中的密码材料，避免普通资料更新清空密码。
- Auth 登录优先校验 `password_hash`；仅当没有 hash 且存在 legacy `password` 时才走明文兼容。
- 同时存在 hash 和明文时 hash 优先，hash 算法未知、salt 缺失或迭代次数非法时拒绝登录。

### 历史和监控

| Key | Type | Field/Index | Value | 生命周期 |
| --- | --- | --- | --- | --- |
| `LOG:MPT:<mount>` | HASH | `<connect_time>_<connect_key>` | connection history JSON | 持久，后续可加清理 |
| `LOG:USR:<user>` | HASH | `<connect_time>_<connect_key>` | connection history JSON | 持久，后续可加清理 |
| `LOG:NODE:<node_id>` | HASH | `<ts>_<event>` | node event JSON | 持久或定期清理 |
| `LOG:AUDIT` | LIST | index | `AuditEntry` JSON | `LTRIM` 保留最近 N 条 |
| `LOG:AUDIT:SEQ` | STRING | - | sequence | 持久 |
| `NODE:HISTORY:<node_id>` | LIST | index | node metric JSON | `LTRIM` RAW |
| `NODE:HISTORY:<node_id>:1M` | LIST | index | 1min aggregate JSON | `LTRIM` |
| `NODE:HISTORY:<node_id>:5M` | LIST | index | 5min aggregate JSON | `LTRIM` |
| `MONITOR:REDIS:HISTORY` | LIST | index | Redis metric JSON | `LTRIM` |

后续如果历史查询成为核心能力，建议迁到关系型数据库或时序库；当前作为管理台监控数据，Redis list/hash 可接受。

## Pub/Sub 频道

| Channel | Payload | 发布者 | 订阅者 | 说明 |
| --- | --- | --- | --- | --- |
| `MPT:<mount>` | binary RTCM/raw | server/pull session | client/push/near session | 实时数据通道 |
| `USR:<user>` | binary NMEA/raw | client session | 需要 rover 上行数据的组件 | 用户上行通道 |
| `CASTER:BROADCAST` | `BroadcastMsg` proto JSON | Core/HTTP/Auth | all nodes | 踢下线、状态、relay 操作 |
| `CASTER:CONF` | string category | HTTP config | all nodes | 配置变更通知 |
| `NODE:<node_id>` | `BroadcastMsg` proto JSON | master | target node | relay 任务指令 |
| `AUTH:BROADCAST` | auth broadcast JSON | AuthService | AuthService | 账号连接限制、踢出 |

## Value Schema 约束

配置和状态类 value 优先使用 proto JSON：

- `SourceRecord`
- `AliasRule`
- `AccessGroup`
- `AccessItem`
- `PullRecord`
- `PushRecord`
- `ServerState`
- `ClientState`
- `StreamState`
- `CasterNode`
- `PullState`
- `PushState`
- `AccountRecord`
- `AccountActive`

存储 helper 必须：

- 忽略未知字段。
- 保留 proto field name。
- 补齐默认值。
- 在写入前做必填校验。
- 对 legacy 字段做兼容读取。

## 必填字段建议

### AccountRecord

- `account`
- `state`
- `active`
- `group_uid`
- `connection_limit`
- `type`

写入时补：

- `uid = account`
- `create_time`
- `update_time`
- 默认 `group_uid = default`

### SourceRecord

- `mountpoint`
- `source_group_uid`
- `record_type`
- `decode_type`
- `display_type`

写入时补：

- `uid = mountpoint`
- `create_time`
- `update_time`

### AliasRule

- `uid`
- `alias_name`
- `source_name`
- `enable`
- `visible`

### AccessGroup

- `uid`
- `group_name`
- 六个 allow 开关。

### AccessItem

- `mount_point_name`
- `allow_visible`
- `allow_access`
- `allow_nearby`

### PullRecord/PushRecord

- `uid`
- `login_mpt`
- `type`
- `target_ip`
- `target_port`
- `target_mpt`
- `enabled`

## 迁移策略

### Step 1：只读盘点

- 增加 Redis schema 文档和 key registry。
- 不改变现有读写行为。

### Step 2：账号双写

- HTTP 写 `ACT:RECORD` 后同步 `ACT:ACTIVE`。
- Auth 继续读 `ACT:ACTIVE`。
- 活跃会话仍兼容 `STR:ACTIVE`，但新增 `ACT:SESSION:*` 写入。

### Step 3：账号读路径统一

- AuthService 通过 AccountRepository 读取账号。
- `ACT:ACTIVE` 变成缓存索引，可由 `ACT:RECORD` 重建。

### Step 4：废弃 legacy

- 管理台不再读 `STR:ACTIVE`。
- 清理或迁移旧 `ACT:ACCOUNT`、`STR:ACTIVE`。

## 当前高风险点

- `ACT:RECORD` 与 `ACT:ACTIVE` 分离且缺同步。
- `STR:ACTIVE` 当前作为账号活跃 API，但未看到明确写入点。
- 密码明文存储。
- HTTP CRUD 对多个配置表直接 `body.dump()`，缺少 schema 校验。
- `MPT:GEO`、`USR:GEO` 清理依赖间接逻辑，需确认 stale member 清理。
- 大量 `HGETALL` 用于 SSE 和统计，规模上来后要按订阅频道和分页优化。

## 验收标准

- Redis key 有单一 registry。
- 每个 key 有 owner、TTL、value schema。
- Web 创建账号后 NTRIP 登录行为可预测。
- Redis 中账号密码不再明文新增。
- 所有持久配置写入都有校验和默认值。
- smoke test 覆盖账号创建、登录、禁用、连接数限制。
