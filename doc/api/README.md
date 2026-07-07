# API 文档说明

本目录保存当前 HTTP API、SSE 和 Proto/API/Web 类型同步规则。

| 文件 | 内容 |
| --- | --- |
| `api-reference.md` | HTTP API、SSE channel、状态码和 Redis 数据来源。 |
| `v2-identity-access-api.md` | v2 Account / AccessAccount / Web session API 契约，作为 NC-118 和 NC-120 实现输入。 |
| `api-contract-sync.md` | proto、HTTP JSON、Web TypeScript types 的同步规则和契约检查命令。 |

旧资料中的 JWT 说法均为历史描述。当前 HTTP token 是进程内 Bearer token，不是 JWT。
