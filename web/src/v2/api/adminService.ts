import api from '../../api/client';
import type {
  ConfigPublishIntent,
  ConfigVersion,
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

const USE_MOCK = import.meta.env.VITE_NAVCASTER_V2_MOCK !== 'false';

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

async function getData<T>(url: string, config?: Parameters<typeof api.get>[1]): Promise<T> {
  const response = await api.get(url, config);
  return response.data?.data ?? response.data;
}

async function postData<T>(url: string, body?: unknown): Promise<T> {
  const response = await api.post(url, body ?? {});
  return response.data?.data ?? response.data;
}

async function putData<T>(url: string, body?: unknown): Promise<T> {
  const response = await api.put(url, body ?? {});
  return response.data?.data ?? response.data;
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
  status: string;
  labels?: Record<string, unknown>;
  last_heartbeat_at?: string;
}

interface AdminRuntime {
  runtime_id: string;
  host_id: string;
  name: string;
  desired?: {
    desired_state: DesiredRuntimeState;
    config_version: number;
    worker_count: number;
    version: number;
  };
  actual?: {
    actual_state: string;
    worker_count?: number;
    connections?: number;
    updated_at?: string;
  };
  updated_at: string;
}

interface AdminIntent {
  intent_id: string;
  status: 'accepted' | 'queued';
  runtime_id: string;
  desired_version: number;
}

function normalizeStatus(status: string | undefined): HostSummary['status'] {
  switch (status) {
    case 'online':
    case 'registered':
    case 'running':
      return 'running';
    case 'agent_offline':
    case 'offline':
      return 'offline';
    case 'draining':
      return 'draining';
    case 'failed':
      return 'failed';
    default:
      return 'pending';
  }
}

function toHostSummary(host: AdminHost): HostSummary {
  return {
    id: host.host_id,
    name: host.display_name || host.host_id,
    region: String(host.labels?.region ?? 'local'),
    address: String(host.labels?.address ?? host.host_id),
    status: normalizeStatus(host.status),
    desired_state: host.status === 'disabled' ? 'maintenance' : 'enabled',
    runtime_count: Number(host.labels?.runtime_count ?? 0),
    worker_count: Number(host.labels?.worker_count ?? 0),
    cpu_load: Number(host.labels?.cpu_usage ?? 0),
    memory_used_gb: Number(host.labels?.memory_used_gb ?? 0),
    memory_total_gb: Number(host.labels?.memory_total_gb ?? 0),
    last_heartbeat_at: host.last_heartbeat_at ?? '',
    config_version_id: String(host.labels?.config_version_id ?? 'unassigned'),
  };
}

function toRuntimeSummary(runtime: AdminRuntime): RuntimeSummary {
  const actualState = runtime.actual?.actual_state ?? runtime.desired?.desired_state ?? 'pending';
  const configVersion = runtime.desired?.config_version ? `cfg-${runtime.desired.config_version}` : 'cfg-unassigned';
  return {
    id: runtime.runtime_id,
    host_id: runtime.host_id,
    host_name: runtime.host_id,
    name: runtime.name || runtime.runtime_id,
    kind: 'caster-core',
    status: normalizeStatus(actualState),
    desired_state: runtime.desired?.desired_state ?? 'stopped',
    current_config_version_id: configVersion,
    target_config_version_id: configVersion,
    worker_count: runtime.actual?.worker_count ?? runtime.desired?.worker_count ?? 0,
    active_sessions: runtime.actual?.connections ?? 0,
    restart_intent_count: 0,
    updated_at: runtime.actual?.updated_at ?? runtime.updated_at,
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
    return {
      running_hosts: hosts.filter((host) => normalizeStatus(host.status) === 'running').length,
      offline_hosts: hosts.filter((host) => normalizeStatus(host.status) === 'offline').length,
      running_runtimes: runtimeSummaries.filter((runtime) => runtime.status === 'running').length,
      failed_runtimes: runtimeSummaries.filter((runtime) => runtime.status === 'failed').length,
      draining_workers: 0,
      active_sessions: runtimeSummaries.reduce((sum, runtime) => sum + runtime.active_sessions, 0),
    };
  },
  async listHosts(params) {
    const hosts = await getData<AdminHost[]>('/api/v1/control/hosts');
    return pageFromRows(hosts.map(toHostSummary), params, ['name', 'id', 'region', 'address']);
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
    await putData<AdminRuntime>(`/api/v1/control/runtimes/${command.runtime_id}/desired-state`, {
      desired_state: command.desired_state,
    });
    return makeReceipt(`Desired state ${command.desired_state} queued for ${command.runtime_id}.`);
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
