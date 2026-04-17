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

// Relay start/stop
export async function relayStart(type: 'pull' | 'push', uid: string) {
  const { data } = await api.post(`/api/relays/${type}/start/${encodeURIComponent(uid)}`);
  return data;
}
export async function relayStop(type: 'pull' | 'push', uid: string) {
  const { data } = await api.post(`/api/relays/${type}/stop/${encodeURIComponent(uid)}`);
  return data;
}

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

export interface SourcetableEntry {
  mountpoint: string;
  identifier?: string;
  format?: string;
  format_details?: string;
  country?: string;
  latitude?: string;
  longitude?: string;
}

export async function fetchRemoteSourcetable(host: string, port: number, username?: string, password?: string): Promise<SourcetableEntry[]> {
  const { data } = await api.post('/api/utils/sourcetable', { host, port, username, password });
  return data.mountpoints || [];
}
