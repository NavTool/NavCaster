import axios from 'axios';

const api = axios.create({
  timeout: 10000,
  headers: { 'Content-Type': 'application/json' },
});

// Token management
let authToken: string | null = localStorage.getItem('token');
let authUser: string | null = localStorage.getItem('authUser');

export function setToken(token: string | null) {
  authToken = token;
  if (token) {
    localStorage.setItem('token', token);
  } else {
    localStorage.removeItem('token');
  }
}

export function setAuthUser(username: string | null) {
  authUser = username;
  if (username) {
    localStorage.setItem('authUser', username);
  } else {
    localStorage.removeItem('authUser');
  }
}

export function getAuthUser(): string | null {
  return authUser;
}

export function getToken(): string | null {
  return authToken;
}

export function setBaseURL(url: string) {
  api.defaults.baseURL = url;
  localStorage.setItem('baseURL', url);
}

export function getBaseURL(): string {
  return api.defaults.baseURL || localStorage.getItem('baseURL') || '';
}

// Request interceptor — attach Bearer token
api.interceptors.request.use((config) => {
  if (authToken) {
    config.headers.Authorization = `Bearer ${authToken}`;
  }
  if (authUser) {
    config.headers['X-Auth-User'] = authUser;
  }
  return config;
});

// Response interceptor — handle 401/403 (auth failure -> force re-login)
function redirectToLogin() {
  // Avoid double-redirect when already on login page
  if (window.location.hash.startsWith('#/login')) return;
  setToken(null);
  setAuthUser(null);
  window.location.hash = '#/login';
}

api.interceptors.response.use(
  (response) => response,
  (error) => {
    const status = error.response?.status;
    if (status === 401 || status === 403) {
      redirectToLogin();
    }
    return Promise.reject(error);
  }
);

// Lightweight reachability + auth probe used by route guards / fallback.
// Resolves with true when backend is reachable AND current credentials are
// accepted; false otherwise. On hard auth failure, also clears token.
export async function probeBackend(): Promise<boolean> {
  if (!authToken || !api.defaults.baseURL) return false;
  try {
    await api.get('/api/status/health', { timeout: 5000 });
    return true;
  } catch (err: any) {
    const status = err?.response?.status;
    if (status === 401 || status === 403) {
      // interceptor already redirected
      return false;
    }
    // Network / 5xx: keep token but report unreachable
    return false;
  }
}

export default api;
