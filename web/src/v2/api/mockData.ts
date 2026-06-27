import type {
  ConfigVersion,
  ControlPlaneOverview,
  HostSummary,
  RuntimeDetail,
  RuntimeEvent,
  RuntimeSummary,
  WorkerMetric,
} from './contracts';

export const mockHosts: HostSummary[] = [
  { id: 'host-east-01', name: 'east-ingest-01', region: 'CN-East', address: '10.12.4.21', status: 'running', desired_state: 'enabled', runtime_count: 4, worker_count: 18, cpu_load: 42, memory_used_gb: 37, memory_total_gb: 96, last_heartbeat_at: '2026-06-27T04:48:12Z', config_version_id: 'cfg-20260627-004' },
  { id: 'host-east-02', name: 'east-ingest-02', region: 'CN-East', address: '10.12.4.22', status: 'draining', desired_state: 'maintenance', runtime_count: 3, worker_count: 11, cpu_load: 28, memory_used_gb: 29, memory_total_gb: 96, last_heartbeat_at: '2026-06-27T04:46:55Z', config_version_id: 'cfg-20260627-003' },
  { id: 'host-south-01', name: 'south-relay-01', region: 'CN-South', address: '10.21.8.18', status: 'failed', desired_state: 'enabled', runtime_count: 4, worker_count: 9, cpu_load: 7, memory_used_gb: 18, memory_total_gb: 64, last_heartbeat_at: '2026-06-27T04:10:22Z', config_version_id: 'cfg-20260626-009' },
  { id: 'host-north-01', name: 'north-edge-01', region: 'CN-North', address: '10.31.2.10', status: 'offline', desired_state: 'maintenance', runtime_count: 2, worker_count: 0, cpu_load: 0, memory_used_gb: 0, memory_total_gb: 64, last_heartbeat_at: '2026-06-27T03:59:31Z', config_version_id: 'cfg-20260625-012' },
  { id: 'host-west-01', name: 'west-caster-01', region: 'CN-West', address: '10.41.5.31', status: 'pending', desired_state: 'enabled', runtime_count: 2, worker_count: 4, cpu_load: 13, memory_used_gb: 12, memory_total_gb: 64, last_heartbeat_at: '2026-06-27T04:47:03Z', config_version_id: 'cfg-20260627-004' },
];

export const mockRuntimes: RuntimeSummary[] = [
  { id: 'rt-east-01-core', host_id: 'host-east-01', host_name: 'east-ingest-01', name: 'caster-core-a', kind: 'caster-core', status: 'running', desired_state: 'running', current_config_version_id: 'cfg-20260627-004', target_config_version_id: 'cfg-20260627-004', worker_count: 8, active_sessions: 342, restart_intent_count: 0, updated_at: '2026-06-27T04:48:12Z' },
  { id: 'rt-east-01-http', host_id: 'host-east-01', host_name: 'east-ingest-01', name: 'admin-service-a', kind: 'http-admin', status: 'running', desired_state: 'running', current_config_version_id: 'cfg-20260627-004', target_config_version_id: 'cfg-20260627-004', worker_count: 3, active_sessions: 0, restart_intent_count: 0, updated_at: '2026-06-27T04:47:52Z' },
  { id: 'rt-east-02-core', host_id: 'host-east-02', host_name: 'east-ingest-02', name: 'caster-core-b', kind: 'caster-core', status: 'draining', desired_state: 'draining', current_config_version_id: 'cfg-20260627-003', target_config_version_id: 'cfg-20260627-004', worker_count: 6, active_sessions: 81, restart_intent_count: 1, updated_at: '2026-06-27T04:46:55Z' },
  { id: 'rt-south-01-relay', host_id: 'host-south-01', host_name: 'south-relay-01', name: 'relay-runtime-a', kind: 'relay', status: 'failed', desired_state: 'running', current_config_version_id: 'cfg-20260626-009', target_config_version_id: 'cfg-20260627-004', worker_count: 4, active_sessions: 0, restart_intent_count: 2, updated_at: '2026-06-27T04:10:22Z' },
  { id: 'rt-west-01-core', host_id: 'host-west-01', host_name: 'west-caster-01', name: 'caster-core-west', kind: 'caster-core', status: 'pending', desired_state: 'running', current_config_version_id: 'cfg-20260625-012', target_config_version_id: 'cfg-20260627-004', worker_count: 4, active_sessions: 12, restart_intent_count: 0, updated_at: '2026-06-27T04:47:03Z' },
];

const events: RuntimeEvent[] = [
  { id: 'evt-001', level: 'info', message: 'Desired state accepted by AdminService queue.', created_at: '2026-06-27T04:43:18Z' },
  { id: 'evt-002', level: 'warning', message: 'Runtime is draining existing sessions before config rollout.', created_at: '2026-06-27T04:39:41Z' },
  { id: 'evt-003', level: 'error', message: 'Worker heartbeat gap exceeded 90 seconds.', created_at: '2026-06-27T04:10:22Z' },
];

export const mockWorkers: WorkerMetric[] = [
  { id: 'wk-east-a-01', runtime_id: 'rt-east-01-core', host_name: 'east-ingest-01', name: 'mount-dispatch-01', status: 'running', assigned_mount_points: 48, active_sessions: 86, throughput_kbps: 8840, latency_p95_ms: 34, error_rate: 0.2, updated_at: '2026-06-27T04:48:10Z' },
  { id: 'wk-east-a-02', runtime_id: 'rt-east-01-core', host_name: 'east-ingest-01', name: 'mount-dispatch-02', status: 'running', assigned_mount_points: 52, active_sessions: 103, throughput_kbps: 10220, latency_p95_ms: 39, error_rate: 0.1, updated_at: '2026-06-27T04:48:09Z' },
  { id: 'wk-east-b-01', runtime_id: 'rt-east-02-core', host_name: 'east-ingest-02', name: 'mount-dispatch-03', status: 'draining', assigned_mount_points: 37, active_sessions: 27, throughput_kbps: 2180, latency_p95_ms: 61, error_rate: 0.5, updated_at: '2026-06-27T04:46:50Z' },
  { id: 'wk-south-a-01', runtime_id: 'rt-south-01-relay', host_name: 'south-relay-01', name: 'relay-worker-01', status: 'failed', assigned_mount_points: 22, active_sessions: 0, throughput_kbps: 0, latency_p95_ms: 0, error_rate: 12.4, updated_at: '2026-06-27T04:10:22Z' },
  { id: 'wk-west-a-01', runtime_id: 'rt-west-01-core', host_name: 'west-caster-01', name: 'mount-dispatch-09', status: 'pending', assigned_mount_points: 18, active_sessions: 12, throughput_kbps: 920, latency_p95_ms: 45, error_rate: 0, updated_at: '2026-06-27T04:47:03Z' },
];

export const mockConfigVersions: ConfigVersion[] = [
  { id: 'cfg-20260627-004', label: 'v2-rollout-east-workers', status: 'active', created_by: 'admin', created_at: '2026-06-27T04:02:00Z', applied_hosts: 2, target_hosts: 5, checksum: 'sha256:9c18e8b2', summary: 'Worker drain policy, mount-point shard hints, AdminService rollout contract.' },
  { id: 'cfg-20260627-003', label: 'runtime-drain-window', status: 'superseded', created_by: 'ops', created_at: '2026-06-27T02:21:00Z', applied_hosts: 1, target_hosts: 5, checksum: 'sha256:6f22ad90', summary: 'Introduces explicit draining state and runtime restart intent queue.' },
  { id: 'cfg-20260626-009', label: 'relay-failover-baseline', status: 'failed', created_by: 'ops', created_at: '2026-06-26T18:34:00Z', applied_hosts: 1, target_hosts: 4, checksum: 'sha256:a44d0991', summary: 'Relay worker balancing profile rejected by one host.' },
  { id: 'cfg-20260625-012', label: 'control-plane-bootstrap', status: 'superseded', created_by: 'admin', created_at: '2026-06-25T09:12:00Z', applied_hosts: 5, target_hosts: 5, checksum: 'sha256:2bda612e', summary: 'Initial v2 control-plane host/runtime schema.' },
];

export function makeRuntimeDetail(summary: RuntimeSummary): RuntimeDetail {
  return {
    ...summary,
    image: `navcaster/${summary.kind}:2.0.0-rc`,
    command: summary.kind === 'caster-core' ? 'navcaster-core --worker-mode managed' : 'navcaster-runtime --managed',
    env_profile: summary.host_name.includes('east') ? 'production-east' : 'production-default',
    desired_state_note: 'State changes are submitted as intent records; runtime execution is owned by AdminService and backend workers.',
    recent_events: events.filter((event) => summary.status === 'failed' || event.level !== 'error'),
    workers: mockWorkers.filter((worker) => worker.runtime_id === summary.id),
  };
}

export const mockOverview: ControlPlaneOverview = {
  running_hosts: mockHosts.filter((host) => host.status === 'running').length,
  offline_hosts: mockHosts.filter((host) => host.status === 'offline').length,
  running_runtimes: mockRuntimes.filter((runtime) => runtime.status === 'running').length,
  failed_runtimes: mockRuntimes.filter((runtime) => runtime.status === 'failed').length,
  draining_workers: mockWorkers.filter((worker) => worker.status === 'draining').length,
  active_sessions: mockRuntimes.reduce((sum, runtime) => sum + runtime.active_sessions, 0),
};
