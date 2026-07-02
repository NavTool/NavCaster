import api from './client';

export type AccountRole = 'admin' | 'user' | 'supplier' | string;

export interface AuthSessionSubject {
  username?: string;
  account_id?: string;
  role?: AccountRole;
  status?: string;
  compat_admin?: boolean;
  token?: string;
}

interface Envelope<T> {
  data?: T;
}

function unwrap<T>(payload: Envelope<T> | T): T {
  if (payload && typeof payload === 'object' && 'data' in payload) {
    return (payload as Envelope<T>).data as T;
  }
  return payload as T;
}

export async function getSession(): Promise<AuthSessionSubject> {
  const response = await api.get('/api/v1/auth/session');
  return unwrap<AuthSessionSubject>(response.data);
}
