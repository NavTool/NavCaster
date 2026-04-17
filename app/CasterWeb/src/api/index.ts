import { createHashApi, createReadOnlyHashApi } from './resource';
import type {
  AccountRecord, AccountActive, ServerState, ClientState,
  SourceRecord, StreamState, AliasRule, AccessGroup, AccessItem,
  PullRecord, PullState, PushRecord, PushState, CasterNode,
} from './types';
import api from './client';

export const accountsApi = createHashApi<AccountRecord>('/api/accounts');
export const accountActivesApi = createReadOnlyHashApi<AccountActive>('/api/accounts/active');
export const sourcesApi = createHashApi<SourceRecord>('/api/sources');
export const serversApi = createReadOnlyHashApi<ServerState>('/api/servers');
export const clientsApi = createReadOnlyHashApi<ClientState>('/api/clients');
export const streamsApi = createReadOnlyHashApi<StreamState>('/api/streams');
export const aliasesApi = createHashApi<AliasRule>('/api/aliases');
export const accessGroupsApi = createHashApi<AccessGroup>('/api/access/groups');
export const pullRecordsApi = createHashApi<PullRecord>('/api/relays/pull');
export const pushRecordsApi = createHashApi<PushRecord>('/api/relays/push');
export const nodesApi = createReadOnlyHashApi<CasterNode>('/api/nodes');

// Pull/Push states (read-only, separate endpoints)
export const pullStatesApi = createReadOnlyHashApi<PullState>('/api/relays/pull/status');
export const pushStatesApi = createReadOnlyHashApi<PushState>('/api/relays/push/status');

// Access items scoped by group_uid
export const accessItemsApi = {
  async getAll(groupUid: string) {
    const { data } = await api.get(`/api/access/items/${encodeURIComponent(groupUid)}`);
    return data as Record<string, AccessItem>;
  },
  async create(groupUid: string, body: Partial<AccessItem>) {
    await api.post(`/api/access/items/${encodeURIComponent(groupUid)}`, body);
  },
  async update(groupUid: string, body: Partial<AccessItem>) {
    await api.put(`/api/access/items/${encodeURIComponent(groupUid)}`, body);
  },
  async remove(groupUid: string, itemUid: string) {
    await api.delete(`/api/access/items/${encodeURIComponent(groupUid)}`, { data: { uid: itemUid } });
  },
};

// System status
export async function getSystemStatus() {
  const { data } = await api.get('/api/status');
  return data;
}

export async function getHealthCheck() {
  const { data } = await api.get('/api/status/health');
  return data;
}
