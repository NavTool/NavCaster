# Deployment 文档说明

本目录保存当前部署、HTTP ingress、Redis 版本和运行 smoke 口径。

| 文件 | 内容 |
| --- | --- |
| `ops-runbook.md` | Master lease、cluster、HTTP ingress、relay、Redis、runtime image 和 smoke 故障处理入口。 |
| `http-ingress.md` | HTTP API/SSE 入口、Force_Enable、fixed/sticky 管理入口和 nginx smoke。 |
| `redis.md` | Redis 8.4.0+ 最低版本、8.6.3 验证版本、命令兼容检查和 e2e fixture。 |

`deploy/scripts/generate_full_stack_compose.sh` 可从发布包生成 `dist/docker` 单机
Docker Compose 部署目录。该 full-stack 部署按 v2 容器边界运行 PostgreSQL、Redis、
AdminService、Web 和 Agent；Agent 容器内包含并监督 `navcaster-caster` 子进程。

HTTP 多入口的产品化取舍、round-robin 告警口径和后续拆分任务见
`../design/http-multi-entry-productization.md`。

C++ 本地构建默认使用 `deploy/scripts/build_ninja.ps1` 或 `deploy/scripts/build_ninja.sh`。
