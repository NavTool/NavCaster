import api from './client';
import type { HashRecord } from './types';

/** Generic CRUD for hash-based resources */
export function createHashApi<T>(basePath: string) {
  return {
    async getAll(): Promise<HashRecord<T>> {
      const { data } = await api.get(basePath);
      return data;
    },

    async getOne(id: string): Promise<T> {
      const { data } = await api.get(`${basePath}/${encodeURIComponent(id)}`);
      return data;
    },

    async create(body: Partial<T>): Promise<void> {
      await api.post(basePath, body);
    },

    async update(id: string, body: Partial<T>): Promise<void> {
      await api.put(`${basePath}/${encodeURIComponent(id)}`, body);
    },

    async remove(id: string): Promise<void> {
      await api.delete(`${basePath}/${encodeURIComponent(id)}`);
    },
  };
}

/** Read-only hash resource */
export function createReadOnlyHashApi<T>(basePath: string) {
  return {
    async getAll(): Promise<HashRecord<T>> {
      const { data } = await api.get(basePath);
      return data;
    },

    async getOne(id: string): Promise<T> {
      const { data } = await api.get(`${basePath}/${encodeURIComponent(id)}`);
      return data;
    },
  };
}
