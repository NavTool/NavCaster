import api from '../../api/client';
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
  RuntimeSummary,
  V2AdminServiceContract,
  WorkerMetric,
} from './contracts';
import { makeRuntimeDetail, mockConfigVersions, mockHosts, mockOverview, mockRuntimes, mockWorkers } from './mockData';

export const V2_ADMIN_SERVICE_MODE = import.meta.env.VITE_NAVCASTER_V2_MOCK === 'true' ? 'mock' : 'live';
const USE_MOCK = V2_ADMIN_SERVICE_MODE === 'mock';

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

interface AdminRuntime {
  runtime_id: string;
  host_id: string;
  name: string;
  desired?: AdminDesiredRuntime;
  actual?: AdminActualSnapshot;
  created_at?: string;
  updated_at: string;
}

interface AdminIntent {
  intent_id: string;
  status: 'accepted' | 'queued';
  runtime_id: string;
  desired_version: number;
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
  return {
    id: runtime.runtime_id,
    host_id: runtime.host_id,
    host_name: runtime.host_id,
    name: runtime.name || runtime.runtime_id,
    kind: 'caster-core',
    status: normalizeStatus(actualState),
    desired_state: desiredState,
    actual_state: actualState,
    convergence_status: convergence.status,
    convergence_detail: convergence.detail,
    current_config_version_id: configVersionLabel(actual?.config_version),
    target_config_version_id: configVersionLabel(desired?.config_version),
    desired_version: desired?.version ?? 0,
    observed_desired_version: actual?.observed_desired_version ?? 0,
    desired_worker_count: desired?.worker_count ?? 0,
    actual_worker_count: actual?.worker_count ?? 0,
    worker_count: actual?.worker_count ?? desired?.worker_count ?? 0,
    active_sessions: actual?.connections ?? 0,
    restart_intent_count: 0,
    listen_port: actual?.listen_port ?? desired?.listen_port ?? 0,
    loop_delay_ms_p95: actual?.loop_delay_ms_p95 ?? 0,
    send_bps: actual?.send_bps ?? 0,
    recv_bps: actual?.recv_bps ?? 0,
    process_id: actual?.process_id,
    redis_connected: actual?.redis_connected,
    last_error: actual?.last_error,
    desired_updated_at: asDateString(desired?.updated_at),
    actual_updated_at: asDateString(actual?.updated_at),
    last_metric_at: asDateString(actual?.updated_at),
    updated_at: updatedAt,
  };
}

function toIntentReceipt(intent: AdminIntent, message: string): IntentReceipt {
  return {
    intent_id: intent.intent_id,
    accepted_at: new Date().toISOString(),
    status: intent.status,
    message,
  };
}

const mockAdminService: V2AdminServiceContract = {
  async getOverview() { return mockOverview; },
  async listHosts(params) { return filterAndPage(mockHosts, params, ['name', 'id', 'region', 'address']); },
  async listRuntimes(params) {
    const rows = params.hostId ? mockRuntimes.filter((runtime) => runtime.host_id === params.hostId) : mockRuntimes;
    return filterAndPage(rows, params, ['name', 'id', 'host_name', 'kind']);
  },
  async getRuntime(runtimeId) {
    const runtime = mockRuntimes.find((item) => item.id === runtimeId) ?? mockRuntimes[0];
    return makeRuntimeDetail(runtime);
  },
  async listWorkers(params) {
    const rows = params.runtimeId ? mockWorkers.filter((worker) => worker.runtime_id === params.runtimeId) : mockWorkers;
    return filterAndPage(rows, params, ['name', 'id', 'host_name']);
  },
  async listConfigVersions(params) {
    const rows = mockConfigVersions as Array<ConfigVersion & { name?: string }>;
    return filterAndPage(rows, params, ['label', 'id', 'summary']);
  },
  async setRuntimeDesiredState(command) { return makeReceipt(`Desired state ${command.desired_state} queued for ${command.runtime_id}.`); },
  async submitRuntimeAction(intent) { return makeReceipt(`Action intent ${intent.action} queued for ${intent.runtime_id}.`); },
  async publishConfigVersion(intent) { return makeReceipt(`Config ${intent.config_version_id} publish intent accepted for ${intent.target_scope}.`); },
};

const liveAdminService: V2AdminServiceContract = {
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
    return makeRuntimeDetail(toRuntimeSummary(runtime));
  },
  async listWorkers(params): Promise<PageResult<WorkerMetric>> {
    return pageFromRows([], params, ['name', 'id', 'host_name']);
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
    const action = intent.action === 'drain-workers' ? 'drain' : intent.action === 'roll-config' ? 'restart' : intent.action;
    const receipt = await postData<AdminIntent>(`/api/v1/control/runtimes/${intent.runtime_id}/actions/${action}`, { reason: intent.reason });
    return toIntentReceipt(receipt, `Action intent ${action} queued for ${intent.runtime_id}.`);
  },
  async publishConfigVersion(intent: ConfigPublishIntent) {
    return makeReceipt(`Config ${intent.config_version_id} publish intent accepted for ${intent.target_scope}.`);
  },
};

export const v2AdminService: V2AdminServiceContract = USE_MOCK ? mockAdminService : liveAdminService;
