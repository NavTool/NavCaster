# NavCaster API Contract Sync

更新时间：2026-06-16

本文档定义 `proto/caster`、HTTP JSON、`web/src/api/types.ts` 和 API 文档之间的
契约同步规则。NC-010 后，关键管理台数据模型由脚本做字段级漂移检查。

## 检查命令

Windows / Linux：

```bash
node tools/contract_check/check_api_contracts.mjs
```

主 CI `.github/workflows/build-and-package.yml` 会在 Web build 前执行该命令。

## 覆盖范围

当前自动检查覆盖：

```text
消息字段
  ServerState
  ClientState
  StreamState
  SourceRecord
  AccountRecord
  AccountActive
  AliasRule
  AccessGroup
  AccessItem
  PullRecord
  PullState
  PushRecord
  PushState
  CasterNode

枚举值
  PullType
  PushType
  SourceRecordType
  SourceDecordType
  SourceDisplayType
  AccountType
  AccountStateType
  AccountActiveState
  AccessState
```

脚本当前检查字段名、枚举成员名和枚举数字值是否同步，不做 TypeScript 类型与
protobuf 标量类型的严格映射。类型级检查留给后续独立任务。

## 允许差异

允许差异必须带原因写在脚本 allowlist 中，并在运行输出里显式打印。新增差异不能
静默通过。

当前允许差异：

| 模型 | 方向 | 字段 | 原因 |
|------|------|------|------|
| `SourceRecord` | proto-only | `ecef_x/ecef_y/ecef_z` | 当前 Web 源表模型不展示或写回 ECEF 坐标。 |
| `AccountActive` | TS-only | `connect_key/anonymous/auth_type/group_uid` | NC-008A/B 后 HTTP 活跃会话来自 `ACT:SESSION:<account>` JSON 扩展，proto 尚未扩展。 |
## 修改规则

```text
修改 proto 字段或枚举：
  - 同步生成 proto/src。
  - 同步 HTTP JSON 输出或解析。
  - 同步 web/src/api/types.ts。
  - 同步 api/api-reference.md。
  - 运行 node tools/contract_check/check_api_contracts.mjs。

修改 HTTP JSON 但暂不修改 proto：
  - 同步 web/src/api/types.ts 和 api/api-reference.md。
  - 若字段属于阶段性 HTTP 扩展，在脚本 allowlist 中写明原因。
  - 后续 proto 收口任务应删除对应 allowlist。

修改 Web 类型：
  - 确认字段来自真实 HTTP JSON 或 proto。
  - 运行契约检查和 Web build。
  - 避免只在 UI 层引入后端不存在的字段名。
```

## 失败处理

契约检查失败时，优先判断差异来源：

```text
真实新增字段：
  同步 proto、HTTP JSON、Web types 和 API 文档。

历史拼写或阶段性扩展：
  只能在任务卡、文档和脚本 allowlist 中写明原因后保留。

误删字段：
  恢复对应契约或补迁移说明。
```
