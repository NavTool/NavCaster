// TypeScript interfaces matching CasterService protobuf messages

// ==================== Enums ====================

export enum PullType {
  PULL_TYPE_UNKNOWN = 0,
  PULL_TYPE_NTRIP_1_0 = 1,
  PULL_TYPE_NTRIP_2_0 = 2,
  PULL_TYPE_TCP_CLIENT = 3,
  PULL_TYPE_TCP_SERVER = 4,
}

export enum PushType {
  PUSH_TYPE_UNKNOWN = 0,
  PUSH_TYPE_NTRIP_1_0 = 1,
  PUSH_TYPE_NTRIP_2_0 = 2,
  PUSH_TYPE_TCP_CLIENT = 3,
  PUSH_TYPE_TCP_SERVER = 4,
}

export enum SourceRecordType {
  SOURCE_RECORD_TYPE_UNKNOWN = 0,
  SOURCE_RECORD_TYPE_REAL = 1,
  SOURCE_RECORD_TYPE_NEAREST = 2,
  SOURCE_RECORD_TYPE_ALIAS = 3,
  SOURCE_RECORD_TYPE_PROXY = 4,
  SOURCE_RECORD_TYPE_GRID = 5,
}

export enum SourceDecordType {
  SOURCE_DECODE_TYPE_UNKNOWN = 0,
  SOURCE_DECODE_TYPE_AUTO = 1,
  SOURCE_DECODE_TYPE_MANUAL = 2,
}

export enum SourceDisplayType {
  SOURCE_DISP_TYPE_UNKNOWN = 0,
  SOURCE_DISP_TYPE_ALWAYS_SHOW = 1,
  SOURCE_DISP_TYPE_ALWAYS_HIDE = 2,
  SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE = 3,
}

export enum AccountType {
  ACCOUNT_TYPE_UNKNOWN = 0,
  ACCOUNT_TYPE_LONG_TERM = 1,
  ACCOUNT_TYPE_EXPIRE_BY_DATE = 2,
  ACCOUNT_TYPE_EXPIRE_BY_USAGE = 3,
}

export enum AccountStateType {
  ACCOUNT_STATE_TYPE_UNKNOWN = 0,
  ACCOUNT_STATE_TYPE_NORMAL = 1,
  ACCOUNT_STATE_TYPE_FROZEN = 2,
  ACCOUNT_STATE_TYPE_EXPIRED = 3,
}

export enum AccountActiveState {
  ACCOUNT_ACTIVE_STATE_UNKNOWN = 0,
  ACCOUNT_ACTIVE_STATE_ACTIVE = 1,
  ACCOUNT_ACTIVE_STATE_INACTIVE = 2,
}

export enum AccessState {
  ACCESS_STATE_DEFALT = 0,
  ACCESS_STATE_ENABLE = 1,
  ACCESS_STATE_DISABLE = 2,
}

// ==================== Core Messages ====================

export interface ServerState {
  uid: string;
  online_time: number;
  update_time: number;
  login_mpt: string;
  alias_mpt: string;
  type: number;
  account: string;
  ip: string;
  port: number;
  online_seconds: number;
  tcp_delay: number;
  ecef_x: number;
  ecef_y: number;
  ecef_z: number;
  position_update_time: number;
}

export interface ClientState {
  uid: string;
  online_time: number;
  update_time: number;
  login_mpt: string;
  alias_mpt: string;
  type: number;
  account: string;
  ip: string;
  port: number;
  online_seconds: number;
  tcp_delay: number;
  ecef_x: number;
  ecef_y: number;
  ecef_z: number;
  position_update_time: number;
  quality: number;
  sat_num: number;
  diff: number;
  distance: number;
}

export interface SourceRecord {
  uid: string;
  create_time: number;
  update_time: number;
  source_group_uid: string;
  mountpoint: string;
  identufier: string;
  format: string;
  format_details: string;
  carrier: string;
  nav_system: string;
  network: string;
  country: string;
  latitude: string;
  longitude: string;
  nmea: string;
  solution: string;
  generator: string;
  compr_encrryp: string;
  authentication: string;
  fee: string;
  bitrate: string;
  misc: string;
  decode_type: SourceDecordType;
  display_type: SourceDisplayType;
  record_type: SourceRecordType;
}

export interface StreamState {
  uid: string;
  online_time: number;
  update_time: number;
  send_total: number;
  send_speed: number;
  recv_total: number;
  recv_speed: number;
}

export interface AccountRecord {
  uid: string;
  create_time: number;
  update_time: number;
  account: string;
  password: string;
  type: AccountType;
  state: AccountStateType;
  active: AccountActiveState;
  connection_limit: number;
  expire_time: number;
  active_time: number;
  register_time: number;
  available_days: number;
  available_seconds: number;
  inactive_time: number;
  contact_name: string;
  contact_person: string;
  contact_info: string;
  remark: string;
  group_uid: string;
}

export interface AccountActive {
  uid: string;
  connect_key?: string;
  create_time?: number;
  update_time: number;
  account: string;
  anonymous?: boolean;
  auth_type?: string;
  online_time: number;
  addr: string;
  port: string;
  group_uid?: string;
}

export interface AliasRule {
  uid: string;
  create_time: number;
  update_time: number;
  enable: boolean;
  source_name: string;
  alias_name: string;
  visible: boolean;
}

export interface AccessGroup {
  uid: string;
  group_name: string;
  create_time: number;
  update_time: number;
  nearest_mpt_enable: boolean;
  nearest_mpt_source_name: string;
  allow_visible_inside_group: boolean;
  allow_access_inside_group: boolean;
  allow_nearby_inside_group: boolean;
  allow_visible_outside_group: boolean;
  allow_access_outside_group: boolean;
  allow_nearby_outside_group: boolean;
}

export interface AccessItem {
  uid: string;
  mount_point_name: string;
  allow_visible: AccessState;
  allow_access: AccessState;
  allow_nearby: AccessState;
  decode_type: SourceDecordType;
  display_type: SourceDisplayType;
}

export interface PullRecord {
  uid: string;
  create_time: number;
  update_time: number;
  login_mpt: string;
  type: PullType;
  target_ip: string;
  target_port: number;
  target_mpt: string;
  target_account: string;
  target_password: string;
  enabled: boolean;
}

export interface PullState {
  uid: string;
  create_time: number;
  update_time: number;
  connect_key: string;
  state: number;
  node_uid: string;
  node_name: string;
}

export interface PushRecord {
  uid: string;
  create_time: number;
  update_time: number;
  login_mpt: string;
  type: PushType;
  target_ip: string;
  target_port: number;
  target_mpt: string;
  target_account: string;
  target_password: string;
  enabled: boolean;
}

export interface PushState {
  uid: string;
  create_time: number;
  update_time: number;
  connect_key: string;
  state: number;
  node_uid: string;
  node_name: string;
}

export interface CasterNode {
  uid: string;
  node_name: string;
  set_version: string;
  tag_version: string;
  run_platform: string;
  cpu_usage: number;
  mem_usage: number;
  queue_delay: number;
  sub_ping_delay: number;
  sub_tcp_delay: number;
  pub_ping_delay: number;
  pub_tcp_delay: number;
  send_total: number;
  send_speed: number;
  recv_total: number;
  recv_speed: number;
  connect_count: number;
  server_count: number;
  client_count: number;
  pull_count?: number;
  push_count?: number;
  online_time: number;
  update_time: number;
  // v2 扩展
  hostname?: string;
  listen_port?: number;
  http_port?: number;
  process_id?: number;
  http_enabled?: boolean;
  // v3 扩展
  sse_clients?: number;
  process_threads?: number;
  last_audit_seq?: number;
  log_level?: string;
}

// ==================== V3 补充类型 ====================

/** 审计日志条目 */
export interface AuditEntry {
  id: number;
  timestamp: number;
  actor: string;
  source_ip?: string;
  node_id?: string;
  action: string;          // 例如 "PUT /api/accounts/:uid"
  target_type?: string;
  target_id?: string;
  payload?: string;        // 脱敏后的 JSON 文本
  result: number;          // HTTP 状态码
  error_message?: string;
}

export interface AuditQueryParams {
  limit?: number;
  cursor?: number;
  actor?: string;
  action?: string;
  target?: string;
}

export interface PagedResult<T> {
  items: T[];
  total?: number;
  count?: number;
  has_more?: boolean;
  next_cursor?: number;
}

/** 环形内存日志条目 */
export interface RingLogEntry {
  timestamp: number;       // 毫秒
  level: number;           // 0=trace 1=debug 2=info 3=warn 4=err 5=critical
  category?: string;
  message: string;
}

/** Redis 监控采样点 */
export interface RedisStatPoint {
  t: number;
  used_memory: number;
  used_memory_rss?: number;
  used_memory_peak?: number;
  mem_fragmentation_ratio?: number;
  total_keys: number;
  ops_per_sec: number;
  total_commands_processed?: number;
  total_connections_received?: number;
  hits: number;
  misses: number;
  hit_rate?: number;
  connected_clients: number;
  blocked_clients?: number;
  input_kbps?: number;
  output_kbps?: number;
}

/** 节点配置快照 */
export interface NodeConfig {
  uid: string;
  section: 'core' | 'service' | 'auth' | string;
  op_seq: number;
  version?: string;
  yaml_text?: string;
  json_text?: string;
}

/** 集群事件 (D8) */
export interface SystemEvent {
  id: string;
  timestamp: number;
  node_id?: string;
  level: 'info' | 'warn' | 'error' | string;
  category?: string;
  message: string;
}

// ==================== Operations Domain (NC-056) ====================

export type AccountRole = 'admin' | 'user' | 'supplier';
export type ResourceStatus = 'active' | 'disabled' | 'deleted' | string;
export type AccessAccountKind = 'user_client' | 'supplier_station' | string;

export interface OperationsAccount {
  account_id: string;
  username: string;
  role: AccountRole | string;
  status: ResourceStatus;
  balance_cents?: number;
  credit_limit_cents?: number;
  concurrency_limit?: number;
  allowed_group_count?: number;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
  remark?: string;
  schema_version?: number;
}

export interface AuthSessionSubject {
  username: string;
  account_id: string;
  role: AccountRole | string;
  status: ResourceStatus;
  compat_admin: boolean;
  token?: string;
  account?: OperationsAccount;
}

export interface MountPointGroup {
  group_id: string;
  name: string;
  status: ResourceStatus;
  billing_multiplier?: number;
  description?: string;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
  schema_version?: number;
}

export interface AccountGroupGrant {
  account_id?: string;
  group_id: string;
  status: ResourceStatus;
  create_time?: number;
  update_time?: number;
  group?: MountPointGroup;
}

export interface MountPointRecord {
  mountpoint: string;
  status?: ResourceStatus;
  hourly_price_cents?: number;
  source_record_mount?: string;
  group_id?: string;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
  schema_version?: number;
}

export interface AccessAccountRecord {
  access_account_id: string;
  owner_account_id: string;
  username: string;
  kind: AccessAccountKind;
  status: ResourceStatus;
  mount_point_group_id: string;
  concurrency_limit?: number;
  expire_time?: number;
  private_remark?: string;
  admin_remark?: string;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
  schema_version?: number;
}

export interface SubscriptionRecord {
  subscription_id: string;
  account_id: string;
  group_ids: string[];
  status: ResourceStatus;
  plan_id?: string;
  plan_snapshot?: SubscriptionPlan;
  price_cents?: number;
  duration_days?: number;
  ledger_id?: string;
  balance_after_cents?: number;
  purchase_time?: number;
  source?: string;
  start_time?: number;
  expire_time?: number;
  granted_by?: string;
  remark?: string;
  create_time?: number;
  update_time?: number;
}

export interface SubscriptionPlan {
  plan_id: string;
  name: string;
  group_ids: string[];
  status: ResourceStatus;
  price_cents?: number;
  duration_days?: number;
  description?: string;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
}

export interface RedeemCodeRecord {
  code: string;
  amount_cents: number;
  status: ResourceStatus;
  redeemed_count?: number;
  max_redemptions?: number;
  expire_time?: number;
  batch_id?: string;
  note?: string;
  last_redeemed_account_id?: string;
  last_redeemed_time?: number;
  create_time?: number;
  update_time?: number;
}

export interface RedeemRedemptionRecord {
  redemption_id: string;
  code: string;
  account_id: string;
  amount_cents: number;
  balance_after_cents: number;
  ledger_id: string;
  source?: string;
  batch_id?: string;
  operator_note?: string;
  create_time?: number;
}

export interface BillingUsageEntry {
  billing_id: string;
  account_id: string;
  access_account_id: string;
  mountpoint: string;
  group_id?: string;
  session_id?: string;
  billing_mode?: string;
  subscription_id?: string;
  subscription_snapshot?: SubscriptionRecord;
  used_seconds?: number;
  stat_cost_cents?: number;
  actual_debit_cents?: number;
  fingerprint?: string;
  create_time?: number;
  start_time?: number;
  end_time?: number;
}

export interface DataPushUsage {
  usage_id: string;
  account_id: string;
  config_id?: string;
  job_id?: string;
  target_mountpoint: string;
  group_id?: string;
  used_seconds?: number;
  stat_cost_cents?: number;
  actual_debit_cents?: number;
  balance_after_cents?: number;
  ledger_id?: string;
  source?: string;
  create_time?: number;
  start_time?: number;
  end_time?: number;
}

export interface DataPushConfig {
  config_id: string;
  name: string;
  target_mountpoint: string;
  group_id?: string;
  status: ResourceStatus;
  execution_mode?: 'ledger_only' | 'relay_push';
  source_mountpoint?: string;
  relay_target_host?: string;
  relay_target_port?: number;
  relay_target_mountpoint?: string;
  relay_target_account?: string;
  relay_target_password?: string;
  relay_push_type?: PushType;
  fixed_hourly_price_cents?: number;
  description?: string;
  create_time?: number;
  update_time?: number;
  delete_time?: number;
}

export interface DataPushJob {
  job_id: string;
  account_id: string;
  config_id: string;
  target_mountpoint: string;
  group_id?: string;
  usage_id: string;
  period?: string;
  used_seconds?: number;
  stat_cost_cents?: number;
  actual_debit_cents?: number;
  balance_after_cents?: number;
  ledger_id?: string;
  status?: string;
  execution_mode?: 'ledger_only' | 'relay_push';
  relay_uid?: string;
  relay_status?: string;
  relay_state?: PushState;
  relay_state_snapshot?: PushState;
  relay_connect_key?: string;
  relay_node_uid?: string;
  relay_node_name?: string;
  runtime_reconcile_action?: string;
  runtime_reconcile_time?: number;
  runtime_reconcile_note?: string;
  runtime_maintenance_action?: string;
  runtime_maintenance_time?: number;
  runtime_unhealthy_since?: number;
  runtime_unhealthy_elapsed_seconds?: number;
  runtime_unhealthy_after_seconds?: number;
  runtime_last_healthy_time?: number;
  relay_runtime_observed?: string;
  failure_reason?: string;
  failure_time?: number;
  runtime_failure_after_seconds?: number;
  runtime_failure_action?: string;
  relay_record_key?: string;
  relay_status_key?: string;
  operator_note?: string;
  create_time?: number;
  update_time?: number;
}

export interface DataPushMaintenanceConfig {
  config_id: string;
  enabled?: boolean;
  interval_seconds?: number;
  unhealthy_after_seconds?: number;
  period_scope?: string;
  create_time?: number;
  update_time?: number;
}

export interface DataPushMaintenanceResult {
  period: string;
  updated_count: number;
  failed_count: number;
  unhealthy_after_seconds?: number;
  manual?: boolean;
  config?: DataPushMaintenanceConfig;
  items: HashRecord<DataPushJob>;
}

export interface StationRecord {
  station_id?: string;
  mountpoint: string;
  display_name?: string;
  first_seen_time?: number;
  last_seen_time?: number;
  total_online_seconds?: number;
  current_online?: boolean;
  last_access_account_id?: string;
  last_supplier_account_id?: string;
  admin_note?: string;
  create_time?: number;
  update_time?: number;
}

export interface SupplierSupplyUsage {
  usage_id: string;
  supplier_account_id: string;
  access_account_id: string;
  station_id?: string;
  mountpoint: string;
  session_id?: string;
  start_time?: number;
  end_time?: number;
  used_seconds?: number;
  earning_cents?: number;
  status?: string;
  create_time?: number;
}

export interface SupplierSettlementRecord {
  settlement_id: string;
  supplier_account_id: string;
  period: string;
  usage_ids?: string[];
  usage_count?: number;
  total_supply_seconds?: number;
  total_earning_cents?: number;
  status?: string;
  operator_note?: string;
  external_ref?: string;
  payment_method?: string;
  payment_ref?: string;
  payment_note?: string;
  paid_time?: number;
  payment_failed_time?: number;
  payment_update_time?: number;
  create_time?: number;
  update_time?: number;
}

export interface RoleDashboard {
  account: OperationsAccount;
  access_account_count: number;
  allowed_group_count: number;
  balance_cents: number;
  concurrency_limit: number;
  usage_count?: number;
  supply_usage_count?: number;
}

export interface SupplierEarningsSummary {
  account_id: string;
  period: string;
  total_supply_seconds: number;
  pending_earning_cents: number;
  pending_payment_cents?: number;
  paid_earning_cents?: number;
  failed_payment_cents?: number;
  cancelled_payment_cents?: number;
  settled_earning_cents: number;
  total_earning_cents: number;
  settlement_count?: number;
}

export interface OperationsAlertPolicy {
  policy_id: string;
  enabled?: boolean;
  low_balance_enabled?: boolean;
  low_balance_threshold_cents?: number;
  negative_balance_enabled?: boolean;
  data_push_failed_enabled?: boolean;
  data_push_failed_threshold?: number;
  data_push_maintenance_disabled_enabled?: boolean;
  supplier_pending_payment_enabled?: boolean;
  supplier_pending_payment_threshold?: number;
  supplier_usage_pending_enabled?: boolean;
  supplier_usage_pending_threshold?: number;
  create_time?: number;
  update_time?: number;
}

export interface OperationsMonitorAlert {
  severity: 'critical' | 'warning' | 'info' | string;
  code: string;
  count?: number;
  threshold?: number;
  message?: string;
}

export interface OperationsMonitorRiskAccount {
  account_id: string;
  username?: string;
  role?: AccountRole | string;
  status?: ResourceStatus;
  balance_cents?: number;
  expire_time?: number;
  risks?: string[];
}

export interface OperationsMonitorDataPushJob {
  job_id: string;
  period?: string;
  account_id?: string;
  config_id?: string;
  target_mountpoint?: string;
  execution_mode?: 'ledger_only' | 'relay_push' | string;
  relay_uid?: string;
  relay_status?: string;
  status?: string;
  failure_reason?: string;
  failure_time?: number;
  runtime_maintenance_time?: number;
  update_time?: number;
  create_time?: number;
}

export interface OperationsMonitor {
  period: string;
  generated_time?: number;
  alert_policy?: OperationsAlertPolicy & { error?: string };
  accounts: {
    total_count: number;
    admin_count?: number;
    user_count?: number;
    supplier_count?: number;
    active_count?: number;
    disabled_count?: number;
    frozen_count?: number;
    expired_count?: number;
    deleted_count?: number;
    negative_balance_count?: number;
    low_balance_count?: number;
    low_balance_threshold_cents?: number;
    total_balance_cents?: number;
    risk_accounts?: OperationsMonitorRiskAccount[];
  };
  subscriptions: {
    total_count: number;
    active_count?: number;
    disabled_count?: number;
    expired_count?: number;
    deleted_count?: number;
  };
  redeem_codes: {
    total_count: number;
    active_count?: number;
    disabled_count?: number;
    expired_count?: number;
    available_count?: number;
  };
  data_push: {
    period: string;
    usage_count?: number;
    total_used_seconds?: number;
    total_debit_cents?: number;
    job_count?: number;
    relay_push_count?: number;
    ledger_only_count?: number;
    status_counts?: Record<string, number>;
    failed_count?: number;
    running_count?: number;
    queued_count?: number;
    maintenance?: DataPushMaintenanceConfig & { error?: string };
    recent_failed_jobs?: OperationsMonitorDataPushJob[];
  };
  supply: {
    period: string;
    usage_count?: number;
    pending_usage_count?: number;
    settled_usage_count?: number;
    total_supply_seconds?: number;
    total_earning_cents?: number;
    pending_earning_cents?: number;
    settled_earning_cents?: number;
    settlements?: {
      count?: number;
      pending_payment_count?: number;
      paid_count?: number;
      payment_failed_count?: number;
      cancelled_count?: number;
      other_count?: number;
      pending_payment_cents?: number;
      paid_cents?: number;
      failed_payment_cents?: number;
      cancelled_payment_cents?: number;
      other_cents?: number;
    };
  };
  alerts: OperationsMonitorAlert[];
}

export interface AccessAccountCreateInput {
  access_account_id: string;
  username: string;
  password: string;
  mount_point_group_id: string;
  concurrency_limit?: number;
  expire_time?: number;
  status?: ResourceStatus;
  private_remark?: string;
}

export interface AccessAccountUpdateInput {
  mount_point_group_id?: string;
  concurrency_limit?: number;
  expire_time?: number;
  status?: ResourceStatus;
  private_remark?: string;
}


// ==================== API Response Helpers ====================

/** GET /api/xxx returns { field: value, ... } where field is the hash key */
export type HashRecord<T> = Record<string, T>;
