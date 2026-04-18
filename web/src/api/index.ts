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

export async function getNodeHistory(nodeId: string, limit?: number, range?: 'raw' | '1m' | '5m'): Promise<NodeHistorySnapshot[]> {
  const params: Record<string, string | number> = {};
  if (limit) params.limit = limit;
  if (range) params.range = range;
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

export async function getStatsDaily(date: string): Promise<StatsOverview & { date: string }> {
  const { data } = await api.get(`/api/stats/daily/${encodeURIComponent(date)}`);
  return data as StatsOverview & { date: string };
}

export async function getMptRanking(params?: { start?: number; end?: number; limit?: number }): Promise<MptRankingItem[]> {
  const { data } = await api.get('/api/stats/mountpoints/ranking', { params });
  return data as MptRankingItem[];
}

export async function getUsrRanking(params?: { start?: number; end?: number; limit?: number }): Promise<UsrRankingItem[]> {
  const { data } = await api.get('/api/stats/users/ranking', { params });
  return data as UsrRankingItem[];
}

// Individual mount/user connection history
export interface ConnectionHistoryItem {
  name: string;
  mount?: string;
  connect_key: string;
  node_id: string;
  type: number;
  account: string;
  host: string;
  port: number;
  connect_time: number;
  last_update: number;
  disconnect_time: number;
  duration: number;
  online: boolean;
}

export async function getMptHistory(mount: string): Promise<ConnectionHistoryItem[]> {
  const { data } = await api.get(`/api/stats/mountpoints/history/${encodeURIComponent(mount)}`);
  return data as ConnectionHistoryItem[];
}

export async function getUsrHistory(user: string): Promise<ConnectionHistoryItem[]> {
  const { data } = await api.get(`/api/stats/users/history/${encodeURIComponent(user)}`);
  return data as ConnectionHistoryItem[];
}

export async function getNodeServerHistory(nodeId: string): Promise<ConnectionHistoryItem[]> {
  const { data } = await api.get(`/api/nodes/logs/servers/${encodeURIComponent(nodeId)}`);
  return data as ConnectionHistoryItem[];
}

export async function getNodeClientHistory(nodeId: string): Promise<ConnectionHistoryItem[]> {
  const { data } = await api.get(`/api/nodes/logs/clients/${encodeURIComponent(nodeId)}`);
  return data as ConnectionHistoryItem[];
}

export interface NodeRuntimeLogItem {
  ts: number;
  timestamp: string;
  level: string;
  logger: string;
  message: string;
}

export interface NodeRuntimeLogResponse {
  node_id: string;
  node_name: string;
  level: string;
  items: NodeRuntimeLogItem[];
}

export async function getNodeRuntimeLogs(nodeId: string, params?: { limit?: number; level?: string }): Promise<NodeRuntimeLogResponse> {
  const { data } = await api.get(`/api/nodes/logs/runtime/${encodeURIComponent(nodeId)}`, { params });
  return data as NodeRuntimeLogResponse;
}

// ==================== Monitoring ====================

export interface RedisMonitorInfo {
  server: { redis_version: string; uptime_in_seconds: number; tcp_port: number; os: string; process_id: number };
  clients: { connected_clients: number; blocked_clients: number; maxclients: number };
  memory: {
    used_memory: number; used_memory_human: string;
    used_memory_rss: number; used_memory_rss_human: string;
    used_memory_peak: number; used_memory_peak_human: string;
    mem_fragmentation_ratio: number;
  };
  stats: {
    total_connections_received: number; total_commands_processed: number;
    instantaneous_ops_per_sec: number; keyspace_hits: number; keyspace_misses: number;
    hit_rate: number; instantaneous_input_kbps: number; instantaneous_output_kbps: number;
  };
  replication: { role: string; connected_slaves: number };
  keyspace: Record<string, unknown>;
  total_keys: number;
}

export interface RedisKeyCategory {
  prefix: string;
  type: string;
  count: number;
  fields: number;
  memory: number;
  description: string;
}

export interface RedisKeysAnalysis {
  categories: RedisKeyCategory[];
  total_keys: number;
  total_memory: number;
}

export interface ClusterMonitorInfo {
  master_node: string;
  total_nodes: number;
  online_nodes: number;
  total_servers: number;
  total_clients: number;
  total_pull: number;
  total_push: number;
  total_cpu: number;
  total_mem: number;
  total_send_speed: number;
  total_recv_speed: number;
  redis_latency_ms: number;
  nodes: {
    uid: string; node_name: string; is_master: boolean;
    cpu: number; mem: number; mpt: number; usr: number;
    pull: number; push: number; conn: number;
    send_speed: number; recv_speed: number;
    send_total: number; recv_total: number;
    set_version: string; tag_version: string; run_platform: string; queue_delay: number;
    sub_ping_delay: number; sub_tcp_delay: number; pub_ping_delay: number; pub_tcp_delay: number;
    listen_port: number; http_port: number; process_id: number;
    online_time: number; update_time: number; uptime_seconds: number;
  }[];
}

export async function getRedisMonitor(): Promise<RedisMonitorInfo> {
  const { data } = await api.get('/api/monitor/redis');
  return data as RedisMonitorInfo;
}

export async function getRedisKeys(): Promise<RedisKeysAnalysis> {
  const { data } = await api.get('/api/monitor/redis/keys');
  return data as RedisKeysAnalysis;
}

export async function getClusterMonitor(): Promise<ClusterMonitorInfo> {
  const { data } = await api.get('/api/monitor/cluster');
  return data as ClusterMonitorInfo;
}

// ==================== Node Config & Control ====================

export interface NodeConfigSchema {
  label: string;
  type: 'number' | 'boolean' | 'string';
  restart_required: boolean;
}

export interface NodeConfigInfo {
  node_id: string;
  node_name: string;
  set_version: string;
  tag_version: string;
  runtime: {
    run_platform: string;
    listen_port: number;
    http_port: number;
    process_id: number;
    online_time: number;
    update_time: number;
  };
  core: Record<string, unknown>;
  service: Record<string, unknown>;
  schema: Record<string, NodeConfigSchema>;
}

export async function getNodeConfig(nodeId: string): Promise<NodeConfigInfo> {
  const { data } = await api.get(`/api/nodes/config/${encodeURIComponent(nodeId)}`);
  return data as NodeConfigInfo;
}

export async function postNodeAction(nodeId: string, action: string, params?: Record<string, unknown>) {
  const { data } = await api.post(`/api/nodes/action/${encodeURIComponent(nodeId)}`, { action, params });
  return data;
}

// ==================== Audit Log ====================

export interface AuditLogEntry {
  ts: number;
  user: string;
  action: string;
  target: string;
  detail: Record<string, unknown>;
  ip: string;
  result: string;
}

export interface AuditLogResponse {
  items: AuditLogEntry[];
  total: number;
}

export async function getAuditLogs(params?: {
  limit?: number; offset?: number; action?: string; user?: string;
}): Promise<AuditLogResponse> {
  const { data } = await api.get('/api/logs/audit', { params });
  return data as AuditLogResponse;
}
