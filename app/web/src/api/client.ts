import axios from 'axios';

const API_BASE_URL = import.meta.env.VITE_NAVCASTER_ADMIN_BASE_URL ?? '';
const TOKEN_KEYS = ['navcasterToken', 'token'];

export function getStoredToken(): string {
  for (const key of TOKEN_KEYS) {
    const token = localStorage.getItem(key);
    if (token) return token;
  }
  return '';
}

export function setStoredToken(token: string) {
  if (token) {
    localStorage.setItem(TOKEN_KEYS[0], token);
  } else {
    localStorage.removeItem(TOKEN_KEYS[0]);
    localStorage.removeItem(TOKEN_KEYS[1]);
  }
}

const api = axios.create({
  baseURL: API_BASE_URL,
  timeout: 10000,
  headers: { 'Content-Type': 'application/json' },
});

api.interceptors.request.use((config) => {
  const token = getStoredToken();
  if (token) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

export default api;
