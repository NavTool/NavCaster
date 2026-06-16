# Deployment 文档说明

本目录保存当前部署、HTTP ingress、Redis 版本和运行 smoke 口径。

| 文件 | 内容 |
| --- | --- |
| `http-ingress.md` | HTTP API/SSE 入口、Force_Enable、fixed/sticky 管理入口和 nginx smoke。 |
| `redis.md` | Redis 8.4.0+ 最低版本、8.6.3 验证版本、命令兼容检查和 e2e fixture。 |

HTTP 多入口的产品化取舍、round-robin 告警口径和后续拆分任务见
`../design/http-multi-entry-productization.md`。

C++ 本地构建默认使用 `deploy/scripts/build_ninja.ps1` 或 `deploy/scripts/build_ninja.sh`。
