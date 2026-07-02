import { useEffect, useRef, useState, useCallback } from 'react';

/**
 * Hook for polling data at a fixed interval.
 * @param fetcher Async function to fetch data
 * @param interval Polling interval in ms (default 2000)
 * @param enabled Whether polling is active
 */
export function usePolling<T>(
  fetcher: () => Promise<T>,
  interval = 2000,
  enabled = true
) {
  const [data, setData] = useState<T | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const fetcherRef = useRef(fetcher);
  fetcherRef.current = fetcher;

  const refresh = useCallback(async () => {
    try {
      const result = await fetcherRef.current();
      setData(result);
      setError(null);
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : 'Unknown error';
      setError(msg);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    if (!enabled) return;
    refresh();
    const timer = setInterval(refresh, interval);
    return () => clearInterval(timer);
  }, [interval, enabled, refresh]);

  return { data, loading, error, refresh };
}
