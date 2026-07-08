import { useCallback, useEffect, useState } from 'react';
import type { PageResult } from '../../api/contracts';
import type { TableFilters } from '../components/TablePage';

export const defaultFilters: TableFilters = {
  search: '',
  status: 'all',
  page: 1,
  pageSize: 10,
};

export function useControlPage<T>(
  loader: (filters: TableFilters) => Promise<PageResult<T>>,
  initialFilters: TableFilters = defaultFilters,
) {
  const [filters, setFilters] = useState<TableFilters>(initialFilters);
  const [items, setItems] = useState<T[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await loader(filters);
      setItems(result.items);
      setTotal(result.total);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : '加载 AdminService 数据失败');
      setItems([]);
      setTotal(0);
    } finally {
      setLoading(false);
    }
  }, [filters, loader]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  return { filters, setFilters, items, total, loading, error, refresh };
}

export function gb(used: number, total: number) {
  if (!total) return used ? `已用 ${used.toFixed(0)} GB` : '-';
  return `${used.toFixed(0)} / ${total.toFixed(0)} GB`;
}

export function formatDateTime(value: string) {
  if (!value) return '-';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '-';
  return new Intl.DateTimeFormat('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
  }).format(date);
}
