# QA 文档说明

本目录保存当前验证门槛、smoke/e2e 矩阵和文档治理检查口径。

| 文件 | 内容 |
| --- | --- |
| `qa-gates.md` | 项目主要构建、contract、schema smoke、Web build 和运行态 smoke 口径。 |
| `v2-qa-matrix.md` | v2 阶段性质量门槛、最小集成 smoke、系统级 QA 矩阵和容量基线口径。 |
| `v2-phase2-contract-qa-gate.md` | v2 Phase 2 真实 PG/Redis、Admin-Agent-Caster-Web 闭环、运行态事件/指标和容量基线 gate。 |
| `v2-phase3-delivery-qa-gate.md` | v2 Phase 3 真实浏览器 smoke、多 Runtime 联调、PG/Redis 一致性、控制指令生命周期、Runtime events/actual/metrics、Caster workers/pubsub 容量基线和交付审查 gate。 |
| `v2-phase3-system-qa-matrix.md` | v2 Phase 3 系统级 QA 矩阵，覆盖真实浏览器、多 Runtime、PG/Redis 一致性、控制 intent、Runtime actual/events、Caster workers/pubsub 和 Reviewer 证据。 |

团队级流程和准入记录仍保存在 `_team/QUALITY_GATES.md`、`_team/qa` 和 `_team/reviews`。
