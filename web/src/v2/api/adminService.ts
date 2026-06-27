import api from '../../api/client';
import type {
  ConfigPublishIntent,
  ConfigVersion,
  IntentReceipt,
  PageRequest,
  PageResult,
  RuntimeActionIntent,
  RuntimeDesiredStateCommand,
  RuntimeDetail,
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
  async getOverview() { const { data } = await api.get('/api/v2/admin/control-plane/overview'); return data; },
  async listHosts(params) { const { data } = await api.get('/api/v2/admin/control-plane/hosts', { params }); return data; },
  async listRuntimes(params) { const { data } = await api.get('/api/v2/admin/control-plane/runtimes', { params }); return data; },
  async getRuntime(runtimeId: string): Promise<RuntimeDetail> { const { data } = await api.get(`/api/v2/admin/control-plane/runtimes/${runtimeId}`); return data; },
  async listWorkers(params): Promise<PageResult<WorkerMetric>> { const { data } = await api.get('/api/v2/admin/control-plane/workers', { params }); return data; },
  async listConfigVersions(params) { const { data } = await api.get('/api/v2/admin/control-plane/config-versions', { params }); return data; },
  async setRuntimeDesiredState(command: RuntimeDesiredStateCommand) { const { data } = await api.post(`/api/v2/admin/control-plane/runtimes/${command.runtime_id}/desired-state`, command); return data; },
  async submitRuntimeAction(intent: RuntimeActionIntent) { const { data } = await api.post(`/api/v2/admin/control-plane/runtimes/${intent.runtime_id}/actions`, intent); return data; },
  async publishConfigVersion(intent: ConfigPublishIntent) { const { data } = await api.post(`/api/v2/admin/control-plane/config-versions/${intent.config_version_id}/publish-intents`, intent); return data; },
};

export const v2AdminService: V2AdminServiceContract = USE_MOCK ? mockAdminService : liveAdminService;
