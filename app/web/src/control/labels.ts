import type { ConvergenceStatus, ControlStatus, DesiredRuntimeState, RuntimeActionIntent, RuntimeKind } from '../api/contracts';
import type { AccountRole, AccountStatus, AccessAccountStatus } from '../api/identity';

const controlStatusLabels: Record<ControlStatus, string> = {
  pending: '待处理',
  running: '运行中',
  failed: '失败',
  draining: '排空中',
  offline: '离线',
  stopped: '已停止',
  unknown: '未知',
};

const convergenceStatusLabels: Record<ConvergenceStatus, string> = {
  converged: '已收敛',
  pending: '收敛中',
  failed: '收敛失败',
};

const desiredRuntimeStateLabels: Record<DesiredRuntimeState, string> = {
  running: '运行中',
  draining: '排空中',
  stopped: '已停止',
};

const runtimeActionLabels: Record<RuntimeActionIntent['action'], string> = {
  start: '启动',
  stop: '停止',
  restart: '重启',
  drain: '排空',
  undrain: '恢复接入',
};

const runtimeKindLabels: Record<RuntimeKind, string> = {
  'caster-core': 'Caster 核心',
  'http-admin': 'HTTP 管理',
  relay: '转发',
  collector: '采集器',
  unknown: '未知',
};

const accountRoleLabels: Record<AccountRole, string> = {
  admin: '管理员',
  user: '普通用户',
};

const accountStatusLabels: Record<AccountStatus | AccessAccountStatus, string> = {
  active: '启用',
  disabled: '停用',
  deleted: '已删除',
  pending_verification: '待验证',
};

const createdViaLabels: Record<string, string> = {
  admin: '管理员创建',
  self_service: '自助注册',
};

const configStatusLabels: Record<string, string> = {
  draft: '草稿',
  active: '生效中',
  superseded: '已替换',
  failed: '失败',
};

const eventLevelLabels: Record<string, string> = {
  info: '信息',
  warning: '警告',
  error: '错误',
};

const hostDesiredStateLabels: Record<string, string> = {
  enabled: '启用',
  maintenance: '维护中',
  disabled: '停用',
};

const apiErrorLabels: Record<string, string> = {
  'Network Error': '网络错误，请检查 AdminService 是否可用',
  'Failed to load AdminService data': '加载 AdminService 数据失败',
  'Failed to load runtime detail': '加载运行时详情失败',
};

export function controlStatusLabel(status: ControlStatus | string) {
  return controlStatusLabels[status as ControlStatus] ?? status;
}

export function convergenceStatusLabel(status: ConvergenceStatus | string) {
  return convergenceStatusLabels[status as ConvergenceStatus] ?? status;
}

export function desiredRuntimeStateLabel(state: DesiredRuntimeState | string) {
  return desiredRuntimeStateLabels[state as DesiredRuntimeState] ?? state;
}

export function runtimeActionLabel(action: RuntimeActionIntent['action'] | string) {
  return runtimeActionLabels[action as RuntimeActionIntent['action']] ?? action;
}

export function runtimeKindLabel(kind: RuntimeKind | string) {
  return runtimeKindLabels[kind as RuntimeKind] ?? kind;
}

export function accountRoleLabel(role: AccountRole | string) {
  return accountRoleLabels[role as AccountRole] ?? role;
}

export function accountStatusLabel(status: AccountStatus | AccessAccountStatus | string) {
  return accountStatusLabels[status as AccountStatus | AccessAccountStatus] ?? status;
}

export function createdViaLabel(value?: string) {
  if (!value) return '-';
  return createdViaLabels[value] ?? value;
}

export function configStatusLabel(status: string) {
  return configStatusLabels[status] ?? status;
}

export function eventLevelLabel(level: string) {
  return eventLevelLabels[level] ?? level;
}

export function hostDesiredStateLabel(state: string) {
  return hostDesiredStateLabels[state] ?? state;
}

export function apiErrorLabel(message: string) {
  return apiErrorLabels[message] ?? message;
}
