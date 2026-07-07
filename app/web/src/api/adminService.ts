import api from './client';
import type {
  ConfigPublishIntent,
  ConfigVersion,
  ConvergenceStatus,
  DesiredRuntimeState,
  HostSummary,
  IntentReceipt,
  PageRequest,
  PageResult,
  RuntimeActionIntent,
  RuntimeDesiredStateCommand,
  RuntimeDetail,
  RuntimeEvent,
  RuntimeSummary,
  AdminServiceContract,
  WorkerMetric,
} from './contracts';

function filterAndPage<T extends { status?: string; name?: string; id: string }>(
  rows: T[],
  params: PageRequest,
  searchFields: Array<keyof T> = ['name', 'id'],
): PageResult<T> {
  const query = params.search?.trim().toLowerCase();
  const status = params.status && params.status !== 'all' ? params.status : '';
  const filtered = rows.filter((row) => {
    const statusMatched = !status || row.status === status;
    const searchMatched = !query || searchFields.some((field) => String(row[field] ?? '').toLowerCase().includes(query));
    return statusMatched && searchMatched;
  });
  const page = Math.max(params.page, 1);
  const pageSize = Math.max(params.pageSize, 1);
  const start = (page - 1) * pageSize;
  return { items: filtered.slice(start, start + pageSize), page, pageSize, total: filtered.length };
}

function makeReceipt(message: string): IntentReceipt {
  return { intent_id: `intent-${Date.now().toString(36)}`, accepted_at: new Date().toISOString(), status: 'accepted', message };
}

interface Envelope<T> {
  data?: T;
  page?: {
    limit?: number;
    offset?: number;
    total?: number;
  };
}

function unwrapEnvelope<T>(payload: Envelope<T> | T): T {
  if (payload && typeof payload === 'object' && 'data' in payload) {
    return (payload as Envelope<T>).data as T;
  }
  return payload as T;
}

async function getData<T>(url: string, config?: Parameters<typeof api.get>[1]): Promise<T> {
  const response = await api.get(url, config);
  return unwrapEnvelope<T>(response.data);
}

async function postData<T>(url: string, body?: unknown): Promise<T> {
  const response = await api.post(url, body ?? {});
  return unwrapEnvelope<T>(response.data);
}

async function putData<T>(url: string, body?: unknown): Promise<T> {
  const response = await api.put(url, body ?? {});
  return unwrapEnvelope<T>(response.data);
}

function pageFromRows<T extends { status?: string; name?: string; id: string }>(
  rows: T[],
  params: PageRequest,
  searchFields: Array<keyof T> = ['name', 'id'],
): PageResult<T> {
  return filterAndPage(rows, params, searchFields);
}

interface AdminHost {
  host_id: string;
  display_name: string;
  agent_id?: string;
  status: string;
  labels?: Record<string, unknown>;
  last_heartbeat_at?: string;
  created_at?: string;
  updated_at?: string;
}

interface AdminDesiredRuntime {
  desired_state: string;
  config_version: number;
  listen_port: number;
  worker_count: number;
  max_worker_count?: number;
  restart_policy?: string;
  draining?: boolean;
  version: number;
  generation?: number;
  updated_at?: string;
}

interface AdminActualSnapshot {
  actual_state: string;
  process_id?: number;
  config_version?: number;
  config_path?: string;
  config_checksum?: string;
  listen_port?: number;
  worker_count?: number;
  connections?: number;
  mounts?: number;
  sources?: number;
  clients?: number;
  send_bps?: number;
  recv_bps?: number;
  loop_delay_ms_p95?: number;
  redis_connected?: boolean;
  last_error?: string;
  observed_desired_version?: number;
  started_at?: string;
  updated_at?: string;
  last_exit_code?: number;
}

interface AdminRuntimeEvent {
  event_id?: string;
  runtime_id: string;
  type?: string;
  severity?: string;
  desired_version?: number;
  process_id?: number;
  message?: string;
  occurred_at?: string;
  created_at?: string;
}

interface AdminRuntime {
  runtime_id: string;
  host_id: string;
  name: string;
  desired?: AdminDesiredRuntime;
  actual?: AdminActualSnapshot;
  events?: AdminRuntimeEvent[];
  recent_events?: AdminRuntimeEvent[];
  created_at?: string;
  updated_at: string;
}

interface AdminIntent {
  intent_id: string;
  status: 'accepted' | 'queued' | 'pending' | 'applying' | 'completed' | 'failed';
  runtime_id: string;
  desired_version: number;
  created_at?: string;
  accepted_at?: string;
  message?: string;
}

function asDateString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function normalizeStatus(status: string | undefined): HostSummary['status'] {
  switch (status) {
    case 'online':
    case 'registered':
    case 'running':
    case 'starting':
    case 'restarting':
      return 'running';
    case 'agent_offline':
    case 'offline':
      return 'offline';
    case 'draining':
    case 'drained':
      return 'draining';
    case 'stopped':
    case 'stopping':
      return 'stopped';
    case 'failed':
      return 'failed';
    case 'unknown':
    case '':
      return 'unknown';
    default:
      return 'pending';
  }
}

function configVersionLabel(value: number | undefined): string {
  return value && value > 0 ? `cfg-${value}` : 'cfg-unassigned';
}

function numberValue(value: unknown): number {
  const parsed = Number(value ?? 0);
  return Number.isFinite(parsed) ? parsed : 0;
}

function memoryBytesToGb(value: unknown): number {
  const bytes = Number(value ?? 0);
  if (!Number.isFinite(bytes) || bytes <= 0) return 0;
  return bytes / 1024 / 1024 / 1024;
}

function desiredStateFrom(value: string | undefined): DesiredRuntimeState {
  switch (value) {
    case 'running':
    case 'draining':
    case 'stopped':
      return value;
    default:
      return 'stopped';
  }
}

function runtimeKindFrom(runtime: AdminRuntime): RuntimeSummary['kind'] {
  const rawKind = String((runtime as unknown as { kind?: string }).kind ?? '').trim();
  if (rawKind === 'caster-core' || rawKind === 'http-admin' || rawKind === 'relay' || rawKind === 'collector') return rawKind;
  return 'unknown';
}

function actualStale(updatedAt: string): { stale: boolean; detail: string } {
  if (!updatedAt) return { stale: true, detail: 'no actual metric snapshot reported' };
  const timestamp = new Date(updatedAt).getTime();
  if (Number.isNaN(timestamp)) return { stale: true, detail: 'actual metric timestamp is invalid' };
  const ageSeconds = Math.max(0, Math.floor((Date.now() - timestamp) / 1000));
  if (ageSeconds > 90) return { stale: true, detail: `actual metrics stale for ${ageSeconds}s` };
  return { stale: false, detail: `actual metrics observed ${ageSeconds}s ago` };
}

function runtimeConvergence(desired?: AdminDesiredRuntime, actual?: AdminActualSnapshot): { status: ConvergenceStatus; detail: string } {
  if (actual?.actual_state === 'failed' || actual?.last_error) {
    return { status: 'failed', detail: actual.last_error || 'runtime actual state is failed' };
  }
  if (!desired) {
    return actual ? { status: 'pending', detail: 'actual snapshot has no desired state yet' } : { status: 'pending', detail: 'waiting for desired and actual state' };
  }
  if (!actual) {
    return { status: 'pending', detail: 'waiting for first actual snapshot' };
  }
  const desiredState = desiredStateFrom(desired.desired_state);
  const actualState = normalizeStatus(actual.actual_state);
  const stateMatches = desiredState === actualState || (desiredState === 'draining' && actualState === 'draining');
  const configMatches = !desired.config_version || !actual.config_version || desired.config_version === actual.config_version;
  const workerMatches = !desired.worker_count || !actual.worker_count || desired.worker_count === actual.worker_count;
  const versionMatches = !desired.version || !actual.observed_desired_version || actual.observed_desired_version >= desired.version;
  if (stateMatches && configMatches && workerMatches && versionMatches) {
    return { status: 'converged', detail: `desired v${desired.version} observed` };
  }
  return { status: 'pending', detail: `desired v${desired.version} pending actual reconciliation` };
}

function summarizeHostConvergence(status: HostSummary['status'], runtimes: RuntimeSummary[]): { status: ConvergenceStatus; detail: string } {
  if (status === 'failed') return { status: 'failed', detail: 'host status is failed' };
  if (status === 'offline') return { status: 'pending', detail: 'waiting for agent heartbeat' };
  if (runtimes.some((runtime) => runtime.convergence_status === 'failed')) return { status: 'failed', detail: 'one or more runtimes failed' };
  const pending = runtimes.filter((runtime) => runtime.convergence_status === 'pending').length;
  if (pending > 0) return { status: 'pending', detail: `${pending} runtime(s) pending` };
  return { status: 'converged', detail: runtimes.length > 0 ? 'all visible runtimes converged' : 'agent heartbeat accepted' };
}

function toHostSummary(host: AdminHost, runtimes: RuntimeSummary[] = []): HostSummary {
  const labels = host.labels ?? {};
  const status = normalizeStatus(host.status);
  const runtimeRows = runtimes.filter((runtime) => runtime.host_id === host.host_id);
  const metricTimes = runtimeRows
    .map((runtime) => runtime.last_metric_at)
    .filter(Boolean)
    .sort();
  const lastMetricAt = metricTimes.length > 0 ? metricTimes[metricTimes.length - 1] : '';
  const convergence = summarizeHostConvergence(status, runtimeRows);
  return {
    id: host.host_id,
    agent_id: host.agent_id,
    name: host.display_name || host.host_id,
    region: String(labels.region ?? 'local'),
    address: String(labels.address ?? labels.machine_id ?? host.host_id),
    os: String(labels.os ?? ''),
    arch: String(labels.arch ?? ''),
    status,
    desired_state: host.status === 'disabled' ? 'maintenance' : 'enabled',
    actual_state: host.status || 'unknown',
    convergence_status: convergence.status,
    convergence_detail: convergence.detail,
    runtime_count: Number(labels.runtime_count ?? runtimeRows.length),
    worker_count: Number(labels.worker_count ?? runtimeRows.reduce((sum, runtime) => sum + runtime.actual_worker_count, 0)),
    cpu_load: Number(labels.cpu_usage_pct ?? labels.cpu_usage ?? 0),
    memory_used_gb: Number(labels.memory_used_gb ?? memoryBytesToGb(labels.memory_used_bytes)),
    memory_total_gb: Number(labels.memory_total_gb ?? memoryBytesToGb(labels.memory_total_bytes)),
    last_heartbeat_at: host.last_heartbeat_at ?? '',
    last_metric_at: lastMetricAt,
    config_version_id: String(labels.config_version_id ?? 'unassigned'),
  };
}

function toRuntimeSummary(runtime: AdminRuntime): RuntimeSummary {
  const desired = runtime.desired;
  const actual = runtime.actual;
  const actualState = actual?.actual_state ?? 'unknown';
  const desiredState = desiredStateFrom(desired?.desired_state);
  const convergence = runtimeConvergence(desired, actual);
  const updatedAt = actual?.updated_at ?? desired?.updated_at ?? runtime.updated_at ?? runtime.created_at ?? '';
  const stale = actualStale(asDateString(actual?.updated_at));
  return {
    id: runtime.runtime_id,
    host_id: runtime.host_id,
    host_name: runtime.host_id,
    name: runtime.name || runtime.runtime_id,
    kind: runtimeKindFrom(runtime),
    status: normalizeStatus(actualState),
    desired_state: desiredState,
    actual_state: actualState,
    convergence_status: convergence.status,
    convergence_detail: convergence.detail,
    current_config_version_id: configVersionLabel(actual?.config_version),
    target_config_version_id: configVersionLabel(desired?.config_version),
    desired_version: desired?.version ?? 0,
    observed_desired_version: actual?.observed_desired_version ?? 0,
    desired_worker_count: numberValue(desired?.worker_count),
    actual_worker_count: numberValue(actual?.worker_count),
    worker_count: numberValue(actual?.worker_count ?? desired?.worker_count),
    active_sessions: numberValue(actual?.connections),
    restart_intent_count: 0,
    listen_port: numberValue(actual?.listen_port ?? desired?.listen_port),
    loop_delay_ms_p95: numberValue(actual?.loop_delay_ms_p95),
    send_bps: numberValue(actual?.send_bps),
    recv_bps: numberValue(actual?.recv_bps),
    process_id: actual?.process_id,
    redis_connected: actual?.redis_connected,
    last_error: actual?.last_error,
    stale: stale.stale,
    stale_detail: stale.detail,
    mounts: numberValue(actual?.mounts),
    sources: numberValue(actual?.sources),
    clients: numberValue(actual?.clients),
    desired_updated_at: asDateString(desired?.updated_at),
    actual_updated_at: asDateString(actual?.updated_at),
    last_metric_at: asDateString(actual?.updated_at),
    updated_at: updatedAt,
  };
}

function eventLevel(severity: string | undefined): RuntimeEvent['level'] {
  switch (severity) {
    case 'error':
    case 'failed':
      return 'error';
    case 'warning':
    case 'warn':
      return 'warning';
    default:
      return 'info';
  }
}

function toRuntimeEvent(event: AdminRuntimeEvent, index: number): RuntimeEvent {
  const createdAt = event.occurred_at ?? event.created_at ?? '';
  const message = event.message || event.type || 'runtime event';
  return {
    id: event.event_id || `${event.runtime_id}-${createdAt || index}`,
    runtime_id: event.runtime_id,
    level: eventLevel(event.severity),
    message,
    type: event.type,
    desired_version: event.desired_version,
    process_id: event.process_id,
    created_at: createdAt,
  };
}

function toWorkerMetric(runtime: RuntimeSummary): WorkerMetric {
  const throughputKbps = (runtime.send_bps + runtime.recv_bps) / 1000;
  return {
    id: `${runtime.id}:worker-pool`,
    runtime_id: runtime.id,
    host_name: runtime.host_name,
    name: `${runtime.name} worker pool`,
    status: runtime.status,
    assigned_mount_points: runtime.mounts,
    active_sessions: runtime.active_sessions,
    throughput_kbps: Number.isFinite(throughputKbps) ? throughputKbps : 0,
    latency_p95_ms: runtime.loop_delay_ms_p95,
    error_rate: runtime.stale ? 100 : runtime.loop_delay_ms_p95,
    updated_at: runtime.last_metric_at || runtime.updated_at,
  };
}

function toRuntimeDetail(runtime: AdminRuntime): RuntimeDetail {
  const summary = toRuntimeSummary(runtime);
  const events = runtime.recent_events ?? runtime.events ?? [];
  return {
    ...summary,
    image: summary.current_config_version_id,
    command: runtime.actual?.config_path || 'Not reported',
    env_profile: runtime.actual?.config_checksum || 'Not reported',
    desired_state_note: summary.convergence_detail,
    recent_events: events.map(toRuntimeEvent),
    workers: [],
  };
}

function toIntentReceipt(intent: AdminIntent, message: string): IntentReceipt {
  return {
    intent_id: intent.intent_id,
    accepted_at: intent.accepted_at ?? intent.created_at ?? new Date().toISOString(),
    status: intent.status === 'queued' ? 'queued' : 'accepted',
    message: intent.message || message,
  };
}

const liveAdminService: AdminServiceContract = {
  async getOverview() {
    const [hosts, runtimes] = await Promise.all([getData<AdminHost[]>('/api/v1/control/hosts'), getData<AdminRuntime[]>('/api/v1/control/runtimes')]);
    const runtimeSummaries = runtimes.map(toRuntimeSummary);
    const hostSummaries = hosts.map((host) => toHostSummary(host, runtimeSummaries));
    return {
      running_hosts: hostSummaries.filter((host) => host.status === 'running').length,
      offline_hosts: hostSummaries.filter((host) => host.status === 'offline').length,
      running_runtimes: runtimeSummaries.filter((runtime) => runtime.status === 'running').length,
      failed_runtimes: runtimeSummaries.filter((runtime) => runtime.status === 'failed').length,
      draining_workers: 0,
      active_sessions: runtimeSummaries.reduce((sum, runtime) => sum + runtime.active_sessions, 0),
    };
  },
  async listHosts(params) {
    const [hosts, runtimes] = await Promise.all([getData<AdminHost[]>('/api/v1/control/hosts'), getData<AdminRuntime[]>('/api/v1/control/runtimes')]);
    const runtimeSummaries = runtimes.map(toRuntimeSummary);
    return pageFromRows(hosts.map((host) => toHostSummary(host, runtimeSummaries)), params, ['name', 'id', 'region', 'address']);
  },
  async listRuntimes(params) {
    const runtimes = await getData<AdminRuntime[]>('/api/v1/control/runtimes');
    const rows = runtimes.map(toRuntimeSummary).filter((runtime) => !params.hostId || runtime.host_id === params.hostId);
    return pageFromRows(rows, params, ['name', 'id', 'host_name', 'kind']);
  },
  async getRuntime(runtimeId: string): Promise<RuntimeDetail> {
    const runtime = await getData<AdminRuntime>(`/api/v1/control/runtimes/${runtimeId}`);
    return toRuntimeDetail(runtime);
  },
  async listRuntimeEvents(runtimeId: string, limit = 100): Promise<RuntimeEvent[]> {
    const events = await getData<AdminRuntimeEvent[]>(`/api/v1/control/runtimes/${runtimeId}/events`, { params: { limit } });
    return events.map(toRuntimeEvent);
  },
  async listWorkers(params): Promise<PageResult<WorkerMetric>> {
    const runtimes = await getData<AdminRuntime[]>('/api/v1/control/runtimes');
    const rows = runtimes
      .map(toRuntimeSummary)
      .filter((runtime) => !params.runtimeId || runtime.id === params.runtimeId)
      .map(toWorkerMetric);
    return pageFromRows(rows, params, ['name', 'id', 'host_name', 'runtime_id']);
  },
  async listConfigVersions(params) {
    return pageFromRows([] as Array<ConfigVersion & { name?: string }>, params, ['label', 'id', 'summary']);
  },
  async setRuntimeDesiredState(command: RuntimeDesiredStateCommand) {
    const runtime = await putData<AdminRuntime>(`/api/v1/control/runtimes/${command.runtime_id}/desired-state`, {
      desired_state: command.desired_state,
    });
    return {
      intent_id: `desired-${command.runtime_id}-${runtime.desired?.version ?? Date.now().toString(36)}`,
      accepted_at: new Date().toISOString(),
      status: 'accepted',
      message: `Desired state ${command.desired_state} accepted for ${command.runtime_id}; waiting for actual convergence.`,
    };
  },
  async submitRuntimeAction(intent: RuntimeActionIntent) {
    const receipt = await postData<AdminIntent>(`/api/v1/control/runtimes/${intent.runtime_id}/actions/${intent.action}`, { reason: intent.reason });
    return toIntentReceipt(receipt, `Action intent ${intent.action} queued for ${intent.runtime_id}; waiting for observed actual state.`);
  },
  async publishConfigVersion(intent: ConfigPublishIntent) {
    return makeReceipt(`Config ${intent.config_version_id} publish intent accepted for ${intent.target_scope}.`);
  },
};

export const adminService: AdminServiceContract = liveAdminService;
