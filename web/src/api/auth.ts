import api, { setAuthUser, setToken } from './client';
import type { AuthSessionSubject } from './types';

export async function login(username: string, password: string): Promise<AuthSessionSubject> {
  const { data } = await api.post('/api/auth/login', { username, password });
  setToken(data.token);
  setAuthUser(data.username || username);
  return data as AuthSessionSubject;
}

export async function logout(): Promise<void> {
  try {
    await api.post('/api/auth/logout');
  } finally {
    setToken(null);
    setAuthUser(null);
  }
}
