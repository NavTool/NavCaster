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

export async function fetchRemoteSourcetable(host: string, port: number, username?: string, password?: string, ntripVersion?: string): Promise<SourcetableEntry[]> {
  const { data } = await api.post('/api/utils/sourcetable', { host, port, username, password, ntrip_version: ntripVersion || '2.0' });
  return data.mountpoints || [];
}

export async function fetchLocalSourcetable(): Promise<SourcetableEntry[]> {
  const { data } = await api.get('/api/utils/sourcetable/local');
  return data.mountpoints || [];
}

// Mountpoint subscriber counts
export const resourceApi = {
  async getMountpointSubscribers(): Promise<Record<string, number>> {
    const { data } = await api.get('/api/mountpoints/subscribers');
    return data;
  },
};

// Node history (time-series snapshots)
export interface NodeHistorySnapshot {
  t: number;
  cpu: number;
  mem: number;
  mpt: number;
  usr: number;
  conn: number;
  send_speed: number;
  recv_speed: number;
  send_total: number;
  recv_total: number;
  q_delay: number;
}

export async function getNodeHistory(nodeId: string, limit?: number): Promise<NodeHistorySnapshot[]> {
  const params = limit ? { limit } : {};
  const { data } = await api.get(`/api/nodes/history/${encodeURIComponent(nodeId)}`, { params });
  return data as NodeHistorySnapshot[];
}

// Statistics
export interface StatsOverview {
  start: number;
  end: number;
  mpt_connections: number;
  usr_connections: number;
  peak_concurrent_mpt: number;
  peak_concurrent_usr: number;
  avg_duration_mpt: number;
  avg_duration_usr: number;
  unique_mountpoints: number;
  unique_users: number;
  hourly_trend: { ts: number; mpt: number; usr: number }[];
}

export interface MptRankingItem {
  name: string;
  total_duration: number;
  connections: number;
  last_seen: number;
}

export interface UsrRankingItem {
  name: string;
  total_duration: number;
  connections: number;
  last_seen: number;
  mount_count: number;
}

export async function getStatsOverview(params?: { start?: number; end?: number; date?: string }): Promise<StatsOverview> {
  const { data } = await api.get('/api/stats/overview', { params });
  return data as StatsOverview;
}

export async function getMptRanking(params?: { start?: number; end?: number; limit?: number }): Promise<MptRankingItem[]> {
  const { data } = await api.get('/api/stats/mountpoints/ranking', { params });
  return data as MptRankingItem[];
}

export async function getUsrRanking(params?: { start?: number; end?: number; limit?: number }): Promise<UsrRankingItem[]> {
  const { data } = await api.get('/api/stats/users/ranking', { params });
  return data as UsrRankingItem[];
}
