import axios from 'axios';

const api = axios.create({
  timeout: 10000,
  headers: { 'Content-Type': 'application/json' },
});

// Token management
let authToken: string | null = localStorage.getItem('token');

export function setToken(token: string | null) {
  authToken = token;
  if (token) {
    localStorage.setItem('token', token);
  } else {
    localStorage.removeItem('token');
  }
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
  return config;
});

// Response interceptor — handle 401
api.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      setToken(null);
      window.location.hash = '#/login';
    }
    return Promise.reject(error);
  }
);

export default api;
