import { useCallback, useEffect, useState } from 'react';
import type { PageResult } from '../api/contracts';
import type { V2TableFilters } from '../components/V2TablePage';

export const defaultFilters: V2TableFilters = {
  search: '',
  status: 'all',
  page: 1,
  pageSize: 10,
};

export function useV2Page<T>(
  loader: (filters: V2TableFilters) => Promise<PageResult<T>>,
  initialFilters: V2TableFilters = defaultFilters,
) {
  const [filters, setFilters] = useState<V2TableFilters>(initialFilters);
  const [items, setItems] = useState<T[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await loader(filters);
      setItems(result.items);
      setTotal(result.total);
    } finally {
      setLoading(false);
    }
  }, [filters, loader]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  return { filters, setFilters, items, total, loading, refresh };
}

export function gb(used: number, total: number) {
  return `${used.toFixed(0)} / ${total.toFixed(0)} GB`;
}

export function formatDateTime(value: string) {
  return new Intl.DateTimeFormat('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
  }).format(new Date(value));
}
