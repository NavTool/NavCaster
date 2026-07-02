# NavCaster Agent v2

`navcaster-agent` is the per-host control-plane agent for NavCaster v2. It uses
HTTP/JSON to register with AdminService, send heartbeat snapshots, poll desired
runtime state, cache the last-known desired state locally, and supervise only
runtime processes started by this agent.

Boundaries:

- Manages local host resources only.
- Does not modify PostgreSQL business data directly.
- Does not carry NTRIP data-plane traffic.
- Does not use gRPC or legacy proto contracts.

The first skeleton stores local state in `agent_state.json`, supports bootstrap
token and `agent_id` / `agent_secret` placeholders, and can supervise either a
configured runtime command or the built-in dummy runtime mode.

Run tests:

```text
go test ./...
```

Example one-shot run:

```text
go run ./cmd/navcaster-agent -config config.example.json -once
```
