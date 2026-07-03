-- NavCaster v2 AdminService foundation schema.
-- PostgreSQL is the source of truth. Redis stores projections, TTL state, and pub/sub only.

CREATE SEQUENCE IF NOT EXISTS runtime_desired_version_seq;
CREATE SEQUENCE IF NOT EXISTS control_intent_seq;
CREATE SEQUENCE IF NOT EXISTS auth_projection_version_seq;

CREATE TABLE IF NOT EXISTS accounts (
    account_id TEXT PRIMARY KEY,
    username TEXT NOT NULL,
    username_norm TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    email TEXT,
    phone TEXT,
    role TEXT NOT NULL DEFAULT 'user',
    status TEXT NOT NULL DEFAULT 'active',
    password_hash TEXT NOT NULL,
    password_algo TEXT NOT NULL DEFAULT 'pbkdf2-sha256',
    password_params JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_via TEXT NOT NULL DEFAULT 'admin',
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    disabled_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,
    CONSTRAINT account_role_name CHECK (role IN ('admin', 'user')),
    CONSTRAINT account_status_name CHECK (status IN ('active', 'disabled', 'deleted', 'pending_verification')),
    CONSTRAINT account_created_via_name CHECK (created_via IN ('admin', 'self_service')),
    CONSTRAINT account_username_norm_not_empty CHECK (length(username_norm) > 0),
    CONSTRAINT account_deleted_has_deleted_at CHECK (status <> 'deleted' OR deleted_at IS NOT NULL)
);

CREATE TABLE IF NOT EXISTS account_name_locks (
    username_norm TEXT PRIMARY KEY,
    account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    first_registered_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    locked_reason TEXT NOT NULL DEFAULT 'registered',
    CONSTRAINT account_name_locks_reason_name CHECK (locked_reason IN ('registered', 'deleted', 'reserved'))
);

CREATE TABLE IF NOT EXISTS access_accounts (
    access_account_id TEXT PRIMARY KEY,
    owner_account_id TEXT NOT NULL REFERENCES accounts(account_id) ON DELETE RESTRICT,
    username TEXT NOT NULL,
    username_norm TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT '',
    kind TEXT NOT NULL DEFAULT 'user_client',
    status TEXT NOT NULL DEFAULT 'active',
    password_hash TEXT NOT NULL,
    password_algo TEXT NOT NULL DEFAULT 'pbkdf2-sha256',
    password_params JSONB NOT NULL DEFAULT '{}'::jsonb,
    expires_at TIMESTAMPTZ,
    mount_point_group_id TEXT,
    concurrency_limit INTEGER NOT NULL DEFAULT 1,
    mount_rules JSONB NOT NULL DEFAULT '[]'::jsonb,
    rate_limit JSONB NOT NULL DEFAULT '{}'::jsonb,
    auth_policy JSONB NOT NULL DEFAULT '{}'::jsonb,
    projection_version BIGINT NOT NULL DEFAULT 0,
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    disabled_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,
    CONSTRAINT access_account_status_name CHECK (status IN ('active', 'disabled', 'deleted')),
    CONSTRAINT access_account_concurrency_positive CHECK (concurrency_limit >= 0),
    CONSTRAINT access_account_username_norm_not_empty CHECK (length(username_norm) > 0),
    CONSTRAINT access_account_deleted_has_deleted_at CHECK (status <> 'deleted' OR deleted_at IS NOT NULL)
);

CREATE TABLE IF NOT EXISTS access_name_locks (
    username_norm TEXT PRIMARY KEY,
    access_account_id TEXT REFERENCES access_accounts(access_account_id) ON DELETE SET NULL,
    owner_account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    first_registered_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    locked_reason TEXT NOT NULL DEFAULT 'registered',
    CONSTRAINT access_name_locks_reason_name CHECK (locked_reason IN ('registered', 'deleted', 'reserved'))
);

CREATE TABLE IF NOT EXISTS web_sessions (
    session_id TEXT PRIMARY KEY,
    account_id TEXT NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',
    issued_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    last_seen_at TIMESTAMPTZ,
    user_agent TEXT NOT NULL DEFAULT '',
    ip_address TEXT NOT NULL DEFAULT '',
    CONSTRAINT web_session_status_name CHECK (status IN ('active', 'revoked', 'expired')),
    CONSTRAINT web_session_expiry_after_issue CHECK (expires_at > issued_at)
);

CREATE TABLE IF NOT EXISTS audit_logs (
    audit_id TEXT PRIMARY KEY,
    actor_account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    actor_username TEXT NOT NULL DEFAULT '',
    action TEXT NOT NULL,
    target_type TEXT NOT NULL,
    target_id TEXT NOT NULL DEFAULT '',
    before_data JSONB,
    after_data JSONB,
    request_id TEXT NOT NULL DEFAULT '',
    ip_address TEXT NOT NULL DEFAULT '',
    user_agent TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

ALTER TABLE accounts ADD COLUMN IF NOT EXISTS username_norm TEXT;
UPDATE accounts SET username_norm = lower(trim(username)) WHERE username_norm IS NULL OR username_norm = '';
ALTER TABLE accounts ALTER COLUMN username_norm SET NOT NULL;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS email TEXT;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS phone TEXT;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS password_params JSONB NOT NULL DEFAULT '{}'::jsonb;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS created_via TEXT NOT NULL DEFAULT 'admin';
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS disabled_at TIMESTAMPTZ;
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMPTZ;
ALTER TABLE accounts ALTER COLUMN role SET DEFAULT 'user';
ALTER TABLE accounts ALTER COLUMN password_algo SET DEFAULT 'pbkdf2-sha256';

ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS username_norm TEXT;
UPDATE access_accounts SET username_norm = lower(trim(username)) WHERE username_norm IS NULL OR username_norm = '';
ALTER TABLE access_accounts ALTER COLUMN username_norm SET NOT NULL;
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS note TEXT NOT NULL DEFAULT '';
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS password_params JSONB NOT NULL DEFAULT '{}'::jsonb;
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS auth_policy JSONB NOT NULL DEFAULT '{}'::jsonb;
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL;
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS disabled_at TIMESTAMPTZ;
ALTER TABLE access_accounts ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMPTZ;
ALTER TABLE access_accounts ALTER COLUMN owner_account_id SET NOT NULL;
ALTER TABLE access_accounts ALTER COLUMN password_algo SET DEFAULT 'pbkdf2-sha256';

INSERT INTO account_name_locks (username_norm, account_id, first_registered_at, locked_reason)
SELECT username_norm, account_id, created_at, CASE WHEN status = 'deleted' THEN 'deleted' ELSE 'registered' END
FROM accounts
ON CONFLICT (username_norm) DO NOTHING;

INSERT INTO access_name_locks (username_norm, access_account_id, owner_account_id, first_registered_at, locked_reason)
SELECT username_norm, access_account_id, owner_account_id, created_at, CASE WHEN status = 'deleted' THEN 'deleted' ELSE 'registered' END
FROM access_accounts
ON CONFLICT (username_norm) DO NOTHING;

CREATE TABLE IF NOT EXISTS hosts (
    host_id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'registered',
    labels JSONB NOT NULL DEFAULT '{}'::jsonb,
    last_heartbeat_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS agents (
    agent_id TEXT PRIMARY KEY,
    host_id TEXT NOT NULL REFERENCES hosts(host_id) ON DELETE CASCADE,
    agent_secret_hash TEXT NOT NULL,
    version TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'registered',
    last_heartbeat_at TIMESTAMPTZ,
    registered_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (host_id)
);

CREATE TABLE IF NOT EXISTS runtimes (
    runtime_id TEXT PRIMARY KEY,
    host_id TEXT NOT NULL REFERENCES hosts(host_id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'created',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS runtime_desired_states (
    runtime_id TEXT PRIMARY KEY REFERENCES runtimes(runtime_id) ON DELETE CASCADE,
    host_id TEXT NOT NULL REFERENCES hosts(host_id) ON DELETE CASCADE,
    desired_state TEXT NOT NULL,
    config_version BIGINT NOT NULL DEFAULT 0,
    listen_port INTEGER NOT NULL,
    worker_count INTEGER NOT NULL,
    max_worker_count INTEGER NOT NULL,
    restart_policy TEXT NOT NULL DEFAULT 'on_failure',
    draining BOOLEAN NOT NULL DEFAULT false,
    action_intent JSONB NOT NULL DEFAULT '{}'::jsonb,
    version BIGINT NOT NULL,
    generation BIGINT NOT NULL,
    updated_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT runtime_desired_state_name CHECK (desired_state IN ('running', 'stopped', 'draining', 'deleted')),
    CONSTRAINT runtime_restart_policy_name CHECK (restart_policy IN ('never', 'on_failure', 'always')),
    CONSTRAINT runtime_worker_count_positive CHECK (worker_count > 0 AND max_worker_count >= worker_count),
    CONSTRAINT runtime_listen_port_range CHECK (listen_port > 0 AND listen_port <= 65535)
);

CREATE TABLE IF NOT EXISTS runtime_actual_snapshots (
    snapshot_id BIGSERIAL PRIMARY KEY,
    runtime_id TEXT NOT NULL REFERENCES runtimes(runtime_id) ON DELETE CASCADE,
    host_id TEXT NOT NULL REFERENCES hosts(host_id) ON DELETE CASCADE,
    agent_id TEXT REFERENCES agents(agent_id) ON DELETE SET NULL,
    actual_state TEXT NOT NULL,
    process_id INTEGER,
    start_token TEXT,
    config_version BIGINT,
    config_path TEXT NOT NULL DEFAULT '',
    config_checksum TEXT NOT NULL DEFAULT '',
    listen_port INTEGER,
    worker_count INTEGER,
    connections INTEGER NOT NULL DEFAULT 0,
    mounts INTEGER NOT NULL DEFAULT 0,
    sources INTEGER NOT NULL DEFAULT 0,
    clients INTEGER NOT NULL DEFAULT 0,
    send_bps BIGINT NOT NULL DEFAULT 0,
    recv_bps BIGINT NOT NULL DEFAULT 0,
    loop_delay_ms_p95 INTEGER NOT NULL DEFAULT 0,
    redis_connected BOOLEAN NOT NULL DEFAULT false,
    observed_desired_version BIGINT NOT NULL DEFAULT 0,
    last_error TEXT NOT NULL DEFAULT '',
    started_at TIMESTAMPTZ,
    last_exit_code INTEGER,
    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    observed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

ALTER TABLE runtime_actual_snapshots ADD COLUMN IF NOT EXISTS config_path TEXT NOT NULL DEFAULT '';
ALTER TABLE runtime_actual_snapshots ADD COLUMN IF NOT EXISTS config_checksum TEXT NOT NULL DEFAULT '';
ALTER TABLE runtime_actual_snapshots ADD COLUMN IF NOT EXISTS started_at TIMESTAMPTZ;
ALTER TABLE runtime_actual_snapshots ADD COLUMN IF NOT EXISTS last_exit_code INTEGER;

CREATE TABLE IF NOT EXISTS config_versions (
    config_version BIGINT PRIMARY KEY,
    status TEXT NOT NULL DEFAULT 'draft',
    payload JSONB NOT NULL,
    checksum TEXT NOT NULL,
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    published_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT config_version_status_name CHECK (status IN ('draft', 'published', 'archived', 'rolled_back'))
);

CREATE TABLE IF NOT EXISTS config_releases (
    release_id BIGSERIAL PRIMARY KEY,
    config_version BIGINT NOT NULL REFERENCES config_versions(config_version) ON DELETE RESTRICT,
    target_runtime_id TEXT REFERENCES runtimes(runtime_id) ON DELETE CASCADE,
    status TEXT NOT NULL DEFAULT 'pending',
    released_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    released_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    CONSTRAINT config_release_status_name CHECK (status IN ('pending', 'published', 'rolled_back', 'failed'))
);

CREATE TABLE IF NOT EXISTS control_intents (
    intent_id TEXT PRIMARY KEY,
    request_id TEXT NOT NULL,
    runtime_id TEXT REFERENCES runtimes(runtime_id) ON DELETE CASCADE,
    host_id TEXT REFERENCES hosts(host_id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'accepted',
    desired_version BIGINT NOT NULL DEFAULT 0,
    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    projected_at TIMESTAMPTZ,
    observed_at TIMESTAMPTZ,
    superseded_at TIMESTAMPTZ,
    failed_at TIMESTAMPTZ,
    failure_reason TEXT NOT NULL DEFAULT '',
    CONSTRAINT control_intent_status_name CHECK (status IN ('accepted', 'projected', 'observed', 'superseded', 'failed'))
);

ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT now();
ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS projected_at TIMESTAMPTZ;
ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS observed_at TIMESTAMPTZ;
ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS superseded_at TIMESTAMPTZ;
ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS failed_at TIMESTAMPTZ;
ALTER TABLE control_intents ADD COLUMN IF NOT EXISTS failure_reason TEXT NOT NULL DEFAULT '';
ALTER TABLE control_intents DROP CONSTRAINT IF EXISTS control_intent_status_name;
UPDATE control_intents SET status = 'projected' WHERE status IN ('pending', 'applying');
UPDATE control_intents SET status = 'observed' WHERE status = 'completed';
ALTER TABLE control_intents ADD CONSTRAINT control_intent_status_name CHECK (status IN ('accepted', 'projected', 'observed', 'superseded', 'failed'));

CREATE TABLE IF NOT EXISTS runtime_events (
    event_id TEXT PRIMARY KEY,
    runtime_id TEXT NOT NULL REFERENCES runtimes(runtime_id) ON DELETE CASCADE,
    host_id TEXT NOT NULL REFERENCES hosts(host_id) ON DELETE CASCADE,
    agent_id TEXT REFERENCES agents(agent_id) ON DELETE SET NULL,
    type TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT '',
    desired_version BIGINT NOT NULL DEFAULT 0,
    process_id INTEGER,
    message TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    ingested_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS operation_audit_logs (
    audit_id BIGSERIAL PRIMARY KEY,
    request_id TEXT NOT NULL,
    actor_type TEXT NOT NULL,
    actor_id TEXT NOT NULL,
    action TEXT NOT NULL,
    target_type TEXT NOT NULL,
    target_id TEXT NOT NULL,
    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_accounts_username_norm ON accounts(username_norm);
CREATE INDEX IF NOT EXISTS idx_accounts_role_status ON accounts(role, status);
CREATE INDEX IF NOT EXISTS idx_accounts_created_at ON accounts(created_at DESC);
CREATE UNIQUE INDEX IF NOT EXISTS uq_access_accounts_username_norm ON access_accounts(username_norm);
CREATE INDEX IF NOT EXISTS idx_access_accounts_owner ON access_accounts(owner_account_id);
CREATE INDEX IF NOT EXISTS idx_access_accounts_owner_status ON access_accounts(owner_account_id, status);
CREATE INDEX IF NOT EXISTS idx_access_accounts_updated_at ON access_accounts(updated_at DESC);
CREATE UNIQUE INDEX IF NOT EXISTS uq_web_sessions_token_hash ON web_sessions(token_hash);
CREATE INDEX IF NOT EXISTS idx_web_sessions_account_status ON web_sessions(account_id, status);
CREATE INDEX IF NOT EXISTS idx_web_sessions_expires_at ON web_sessions(expires_at);
CREATE INDEX IF NOT EXISTS idx_audit_logs_created_at ON audit_logs(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_audit_logs_actor ON audit_logs(actor_account_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_audit_logs_target ON audit_logs(target_type, target_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_hosts_status ON hosts(status);
CREATE INDEX IF NOT EXISTS idx_agents_host ON agents(host_id);
CREATE INDEX IF NOT EXISTS idx_runtimes_host ON runtimes(host_id);
CREATE INDEX IF NOT EXISTS idx_runtime_desired_host_version ON runtime_desired_states(host_id, version);
CREATE INDEX IF NOT EXISTS idx_runtime_actual_runtime_observed ON runtime_actual_snapshots(runtime_id, observed_at DESC);
CREATE INDEX IF NOT EXISTS idx_runtime_events_runtime_occurred ON runtime_events(runtime_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_config_releases_runtime ON config_releases(target_runtime_id, released_at DESC);
CREATE INDEX IF NOT EXISTS idx_control_intents_runtime ON control_intents(runtime_id, created_at DESC);
CREATE UNIQUE INDEX IF NOT EXISTS idx_control_intents_request ON control_intents(request_id);
CREATE INDEX IF NOT EXISTS idx_control_intents_status ON control_intents(status);
CREATE INDEX IF NOT EXISTS idx_operation_audit_created ON operation_audit_logs(created_at DESC);
