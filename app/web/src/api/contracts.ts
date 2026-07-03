export type ControlStatus = 'pending' | 'running' | 'failed' | 'draining' | 'offline' | 'stopped' | 'unknown';
export type RuntimeKind = 'caster-core' | 'http-admin' | 'relay' | 'collector' | 'unknown';
export type DesiredRuntimeState = 'running' | 'draining' | 'stopped';
export type ConvergenceStatus = 'converged' | 'pending' | 'failed';

export interface PageRequest {
  page: number;
  pageSize: number;
  search?: string;
  status?: ControlStatus | 'all';
}

export interface PageResult<T> {
  items: T[];
  page: number;
  pageSize: number;
  total: number;
}

export interface HostSummary {
  id: string;
  agent_id?: string;
  name: string;
  region: string;
  address: string;
  os?: string;
  arch?: string;
  status: ControlStatus;
  desired_state: 'enabled' | 'maintenance' | 'disabled';
  actual_state: string;
  convergence_status: ConvergenceStatus;
  convergence_detail: string;
  runtime_count: number;
  worker_count: number;
  cpu_load: number;
  memory_used_gb: number;
  memory_total_gb: number;
  last_heartbeat_at: string;
  last_metric_at: string;
  config_version_id: string;
}

export interface RuntimeSummary {
  id: string;
  host_id: string;
  host_name: string;
  name: string;
  kind: RuntimeKind;
  status: ControlStatus;
  desired_state: DesiredRuntimeState;
  actual_state: string;
  convergence_status: ConvergenceStatus;
  convergence_detail: string;
  current_config_version_id: string;
  target_config_version_id: string;
  desired_version: number;
  observed_desired_version: number;
  desired_worker_count: number;
  actual_worker_count: number;
  worker_count: number;
  active_sessions: number;
  restart_intent_count: number;
  listen_port: number;
  loop_delay_ms_p95: number;
  send_bps: number;
  recv_bps: number;
  process_id?: number;
  redis_connected?: boolean;
  last_error?: string;
  stale: boolean;
  stale_detail: string;
  mounts: number;
  sources: number;
  clients: number;
  desired_updated_at: string;
  actual_updated_at: string;
  last_metric_at: string;
  updated_at: string;
}

export interface RuntimeDetail extends RuntimeSummary {
  image: string;
  command: string;
  env_profile: string;
  desired_state_note: string;
  recent_events: RuntimeEvent[];
  workers: WorkerMetric[];
}

export interface RuntimeEvent {
  id: string;
  runtime_id?: string;
  level: 'info' | 'warning' | 'error';
  message: string;
  type?: string;
  desired_version?: number;
  process_id?: number;
  created_at: string;
}

export interface WorkerMetric {
  id: string;
  runtime_id: string;
  host_name: string;
  name: string;
  status: ControlStatus;
  assigned_mount_points: number;
  active_sessions: number;
  throughput_kbps: number;
  latency_p95_ms: number;
  error_rate: number;
  updated_at: string;
}

export interface ConfigVersion {
  id: string;
  label: string;
  status: 'draft' | 'active' | 'superseded' | 'failed';
  created_by: string;
  created_at: string;
  applied_hosts: number;
  target_hosts: number;
  checksum: string;
  summary: string;
}

export interface ControlPlaneOverview {
  running_hosts: number;
  offline_hosts: number;
  running_runtimes: number;
  failed_runtimes: number;
  draining_workers: number;
  active_sessions: number;
}

export interface RuntimeDesiredStateCommand {
  runtime_id: string;
  desired_state: DesiredRuntimeState;
  reason: string;
}

export interface RuntimeActionIntent {
  runtime_id: string;
  action: 'start' | 'stop' | 'restart' | 'drain' | 'undrain';
  reason: string;
}

export interface ConfigPublishIntent {
  config_version_id: string;
  target_scope: 'all-hosts' | 'selected-hosts';
  host_ids?: string[];
  reason: string;
}

export interface IntentReceipt {
  intent_id: string;
  accepted_at: string;
  status: 'accepted' | 'queued';
  message: string;
}

export interface AdminServiceContract {
  getOverview(): Promise<ControlPlaneOverview>;
  listHosts(params: PageRequest): Promise<PageResult<HostSummary>>;
  listRuntimes(params: PageRequest & { hostId?: string }): Promise<PageResult<RuntimeSummary>>;
  getRuntime(runtimeId: string): Promise<RuntimeDetail>;
  listRuntimeEvents(runtimeId: string, limit?: number): Promise<RuntimeEvent[]>;
  listWorkers(params: PageRequest & { runtimeId?: string }): Promise<PageResult<WorkerMetric>>;
  listConfigVersions(params: PageRequest): Promise<PageResult<ConfigVersion>>;
  setRuntimeDesiredState(command: RuntimeDesiredStateCommand): Promise<IntentReceipt>;
  submitRuntimeAction(intent: RuntimeActionIntent): Promise<IntentReceipt>;
  publishConfigVersion(intent: ConfigPublishIntent): Promise<IntentReceipt>;
}
