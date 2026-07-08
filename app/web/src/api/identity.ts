import api, { setStoredToken } from './client';
import type { PageResult } from './contracts';

export type AccountRole = 'admin' | 'user';
export type AccountStatus = 'active' | 'disabled' | 'deleted' | 'pending_verification';
export type AccessAccountStatus = 'active' | 'disabled' | 'deleted';

interface Envelope<T> {
  data?: T;
  page?: {
    limit?: number;
    offset?: number;
    total?: number;
  };
  error?: {
    code?: string;
    message?: string;
  };
}

export interface Account {
  account_id: string;
  username: string;
  display_name: string;
  email?: string;
  phone?: string;
  role: AccountRole;
  status: AccountStatus;
  created_via?: 'admin' | 'self_service';
  created_at?: string;
  updated_at?: string;
  access_account_count?: number;
  active_session_count?: number;
}

export interface AccessAccount {
  access_account_id: string;
  owner_account_id: string;
  owner_username?: string;
  username: string;
  display_name: string;
  note?: string;
  status: AccessAccountStatus;
  expires_at?: string | null;
  concurrency_limit: number;
  projection_version?: number;
  created_at?: string;
  updated_at?: string;
}

export interface SessionSubject {
  session_id: string;
  account_id: string;
  username: string;
  display_name: string;
  role: AccountRole;
  status: AccountStatus;
  expires_at: string;
}

export interface LoginResponse {
  token: string;
  session_id: string;
  account: Account;
  expires_at: string;
}

export interface PageQuery {
  page: number;
  pageSize: number;
  search?: string;
  status?: string;
}

function unwrap<T>(payload: Envelope<T> | T): T {
  if (payload && typeof payload === 'object' && 'data' in payload) {
    return (payload as Envelope<T>).data as T;
  }
  return payload as T;
}

function unwrapPage<T>(payload: Envelope<T[]> | T[], query: PageQuery): PageResult<T> {
  const envelope = payload as Envelope<T[]>;
  const rows = unwrap<T[]>(payload) ?? [];
  const limit = envelope.page?.limit ?? query.pageSize;
  const offset = envelope.page?.offset ?? Math.max(0, query.page - 1) * query.pageSize;
  return {
    items: rows,
    page: Math.floor(offset / Math.max(limit, 1)) + 1,
    pageSize: limit,
    total: envelope.page?.total ?? rows.length,
  };
}

function pageParams(query: PageQuery) {
  return {
    limit: query.pageSize,
    offset: Math.max(0, query.page - 1) * query.pageSize,
    search: query.search || undefined,
    status: query.status && query.status !== 'all' ? query.status : undefined,
  };
}

function normalizeErrorMessage(message: string) {
  const known: Record<string, string> = {
    'Network Error': '网络错误，请检查服务是否可用',
    'Request failed with status code 400': '请求参数不正确',
    'Request failed with status code 401': '登录已失效，请重新登录',
    'Request failed with status code 403': '没有权限执行该操作',
    'Request failed with status code 404': '请求的资源不存在',
    'Request failed with status code 409': '数据冲突，请刷新后重试',
    'Request failed with status code 500': '服务端内部错误',
  };
  return known[message] ?? message;
}

export function apiErrorMessage(error: unknown): string {
  if (typeof error === 'object' && error !== null && 'response' in error) {
    const response = (error as { response?: { data?: Envelope<unknown> } }).response;
    const apiError = response?.data?.error;
    if (apiError?.message) return normalizeErrorMessage(apiError.message);
    if (apiError?.code) return normalizeErrorMessage(apiError.code);
  }
  return error instanceof Error ? normalizeErrorMessage(error.message) : '请求失败';
}

export async function registerAccount(body: { username: string; password: string; display_name: string }) {
  const response = await api.post('/api/v1/auth/register', body);
  return unwrap<Account>(response.data);
}

export async function login(username: string, password: string) {
  const response = await api.post('/api/v1/auth/login', { username, password });
  const result = unwrap<LoginResponse>(response.data);
  setStoredToken(result.token);
  return result;
}

export async function logout() {
  try {
    await api.post('/api/v1/auth/logout', {});
  } catch {
    // Local logout must clear stale tokens even if the server session already expired.
  } finally {
    setStoredToken('');
  }
}

export async function getSession() {
  const response = await api.get('/api/v1/auth/session');
  return unwrap<SessionSubject>(response.data);
}

export async function listAccounts(query: PageQuery & { role?: AccountRole | 'all' }) {
  const response = await api.get('/api/v1/admin/accounts', {
    params: {
      ...pageParams(query),
      role: query.role && query.role !== 'all' ? query.role : undefined,
    },
  });
  return unwrapPage<Account>(response.data, query);
}

export async function createAccount(body: {
  username: string;
  password: string;
  display_name: string;
  role: AccountRole;
  status: AccountStatus;
}) {
  const response = await api.post('/api/v1/admin/accounts', body);
  return unwrap<Account>(response.data);
}

export async function updateAccountStatus(accountId: string, status: AccountStatus) {
  const response = await api.put(`/api/v1/admin/accounts/${accountId}/status`, { status });
  return unwrap<Account>(response.data);
}

export async function resetAccountPassword(accountId: string, password: string, revoke_sessions = true) {
  const response = await api.put(`/api/v1/admin/accounts/${accountId}/password`, { password, revoke_sessions });
  return unwrap<Account>(response.data);
}

export async function deleteAccount(accountId: string) {
  const response = await api.delete(`/api/v1/admin/accounts/${accountId}`);
  return unwrap<{ account_id: string; status: AccountStatus; disabled_access_account_count: number }>(response.data);
}

export async function listAdminAccessAccounts(query: PageQuery & { owner_account_id?: string }) {
  const response = await api.get('/api/v1/admin/access-accounts', {
    params: {
      ...pageParams(query),
      owner_account_id: query.owner_account_id || undefined,
    },
  });
  return unwrapPage<AccessAccount>(response.data, query);
}

export async function getAdminAccessAccount(accessAccountId: string) {
  const response = await api.get(`/api/v1/admin/access-accounts/${accessAccountId}`);
  return unwrap<AccessAccount>(response.data);
}

export async function getProfile() {
  const response = await api.get('/api/v1/me/profile');
  return unwrap<Account>(response.data);
}

export async function updateProfile(body: { display_name: string; email?: string; phone?: string }) {
  const response = await api.put('/api/v1/me/profile', body);
  return unwrap<Account>(response.data);
}

export async function listMyAccessAccounts(query: PageQuery) {
  const response = await api.get('/api/v1/me/access-accounts', { params: pageParams(query) });
  return unwrapPage<AccessAccount>(response.data, query);
}

export async function getMyAccessAccount(accessAccountId: string) {
  const response = await api.get(`/api/v1/me/access-accounts/${accessAccountId}`);
  return unwrap<AccessAccount>(response.data);
}

export async function createMyAccessAccount(body: {
  username: string;
  password: string;
  display_name: string;
  note?: string;
  concurrency_limit: number;
  expires_at?: string | null;
}) {
  const response = await api.post('/api/v1/me/access-accounts', body);
  return unwrap<AccessAccount>(response.data);
}

export async function updateMyAccessAccount(accessAccountId: string, body: {
  display_name: string;
  note?: string;
  concurrency_limit: number;
  expires_at?: string | null;
}) {
  const response = await api.put(`/api/v1/me/access-accounts/${accessAccountId}`, body);
  return unwrap<AccessAccount>(response.data);
}

export async function updateMyAccessAccountStatus(accessAccountId: string, status: AccessAccountStatus) {
  const response = await api.put(`/api/v1/me/access-accounts/${accessAccountId}/status`, { status });
  return unwrap<AccessAccount>(response.data);
}

export async function updateMyAccessAccountPassword(accessAccountId: string, password: string) {
  const response = await api.put(`/api/v1/me/access-accounts/${accessAccountId}/password`, { password });
  return unwrap<AccessAccount>(response.data);
}

export async function deleteMyAccessAccount(accessAccountId: string) {
  const response = await api.delete(`/api/v1/me/access-accounts/${accessAccountId}`);
  return unwrap<{ access_account_id: string; status: AccessAccountStatus }>(response.data);
}
