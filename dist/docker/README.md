# NavCaster Full-Stack Docker Compose

This directory runs the v2 single-host deployment baseline:

- `postgres`: PostgreSQL source of truth.
- `redis`: projection/cache/TTL state and notification bus.
- `admin`: `navcaster-admin`.
- `web`: nginx static Web console with `/api` proxied to `admin`.
- `agent-1`: `navcaster-agent`; it starts and supervises `navcaster-caster` inside the same container.

## Generate

From the repository root, build a NavCaster package first:

```bash
bash deploy/scripts/package_linux.sh
```

Then generate this deployment directory:

```bash
bash deploy/scripts/generate_full_stack_compose.sh --package-dir dist/<PackageName>
```

The generator copies the package into `dist/docker/package/`. The compose files build images from that local package, so the deployment machine only needs Docker.

## Configure

```bash
cd dist/docker
cp .env.example .env
```

Edit `.env` and replace all `change-me-*` values before exposing the stack.

Default ports:

```text
web:            http://127.0.0.1:8080
admin health:   http://127.0.0.1:18080/api/v1/health
postgres:       127.0.0.1:15432
redis:          127.0.0.1:16379
agent-1 ntrip:  127.0.0.1:4202
agent-1 health: 127.0.0.1:14202
```

## Start

```bash
docker compose --env-file .env build
docker compose --env-file .env up -d
docker compose --env-file .env ps
```

Health checks:

```bash
curl -fsS http://127.0.0.1:18080/api/v1/health
curl -fsS http://127.0.0.1:8080/ >/dev/null
```

## Start A Caster Runtime

After `agent-1` registers, create a runtime on its host:

```bash
curl -fsS -X POST http://127.0.0.1:18080/api/v1/control/runtimes \
  -H 'Content-Type: application/json' \
  -d '{
    "host_id": "host-compose-agent-1",
    "name": "caster-agent-1",
    "desired_state": "running",
    "config_version": 1,
    "listen_port": 4202,
    "worker_count": 4,
    "max_worker_count": 16,
    "restart_policy": "on_failure",
    "start_immediately": true
  }'
```

`agent-1` polls AdminService, starts `navcaster-caster` inside the same container, then reports actual state and metrics back to AdminService.

## Persistence

All persistence is relative to this compose directory:

```text
data/postgres/        PostgreSQL data
data/redis/           Redis AOF/RDB data
data/agents/agent-1/  agent identity, state cache, runtime root, caster working dirs
logs/admin/           admin auxiliary logs
logs/web/             nginx logs
logs/agent-1/         agent/caster auxiliary logs
```

Back up PostgreSQL with `pg_dump` or `pg_dumpall`; do not edit database files in `data/postgres` directly.

## Multiple Agents

Do not use `docker compose --scale agent-1=N`; scaled containers would share state and port bindings. To add more agents, copy the `agent-1` service block, use unique values:

```text
AGENT_N_ID
AGENT_N_HOST_ID
data/agents/agent-N
logs/agent-N
caster NTRIP / health host ports
```

Each agent container still starts and supervises only its own `navcaster-caster` child processes. The agent does not mount the Docker socket and does not manage Docker.
