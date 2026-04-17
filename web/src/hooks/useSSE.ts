import { useEffect, useRef, useState, useCallback } from 'react';
import { getBaseURL, getToken } from '../api/client';

interface UseSSEOptions {
  /** SSE channels to subscribe (comma-separated or "*" for all) */
  channels?: string;
  /** Whether SSE is enabled */
  enabled?: boolean;
  /** Fallback polling interval (ms) when SSE disconnects, 0 to disable */
  fallbackInterval?: number;
}

interface UseSSEResult<T> {
  data: T | null;
  connected: boolean;
  error: string | null;
}

/**
 * Hook for receiving real-time data via Server-Sent Events.
 * Subscribes to specified SSE channel and returns parsed data.
 *
 * @param channel The SSE event name to listen for
 * @param options Configuration options
 */
export function useSSE<T>(
  channel: string,
  options: UseSSEOptions = {}
): UseSSEResult<T> {
  const { channels, enabled = true } = options;

  const [data, setData] = useState<T | null>(null);
  const [connected, setConnected] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const esRef = useRef<EventSource | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout>>();

  const connect = useCallback(() => {
    const baseURL = getBaseURL();
    const token = getToken();
    if (!baseURL || !token) return;

    // Build SSE URL with auth token
    const params = new URLSearchParams();
    params.set('token', token);
    if (channels) params.set('channels', channels);
    const url = `${baseURL}/api/events/stream?${params.toString()}`;

    const es = new EventSource(url);
    esRef.current = es;

    es.onopen = () => {
      setConnected(true);
      setError(null);
    };

    // Listen for the specific channel event
    es.addEventListener(channel, (event) => {
      try {
        const parsed = JSON.parse(event.data) as T;
        setData(parsed);
      } catch {
        // Ignore parse errors
      }
    });

    es.onerror = () => {
      setConnected(false);
      setError('SSE connection lost');
      es.close();
      esRef.current = null;
      // Auto-reconnect after 3 seconds
      reconnectTimer.current = setTimeout(connect, 3000);
    };
  }, [channel, channels]);

  useEffect(() => {
    if (!enabled) return;
    connect();
    return () => {
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      if (esRef.current) {
        esRef.current.close();
        esRef.current = null;
      }
      setConnected(false);
    };
  }, [enabled, connect]);

  return { data, connected, error };
}

/**
 * Hook for receiving multiple SSE channels at once.
 * Returns a map of channel name → latest data.
 */
export function useMultiSSE<T extends Record<string, unknown>>(
  channelNames: string[],
  options: UseSSEOptions = {}
): { data: Partial<T>; connected: boolean; error: string | null } {
  const { enabled = true } = options;

  const [data, setData] = useState<Partial<T>>({});
  const [connected, setConnected] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const esRef = useRef<EventSource | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout>>();
  const channelsStr = channelNames.join(',');

  const connect = useCallback(() => {
    const baseURL = getBaseURL();
    const token = getToken();
    if (!baseURL || !token) return;

    const params = new URLSearchParams();
    params.set('token', token);
    params.set('channels', channelsStr);
    const url = `${baseURL}/api/events/stream?${params.toString()}`;

    const es = new EventSource(url);
    esRef.current = es;

    es.onopen = () => {
      setConnected(true);
      setError(null);
    };

    for (const ch of channelNames) {
      es.addEventListener(ch, (event) => {
        try {
          const parsed = JSON.parse(event.data);
          setData((prev) => ({ ...prev, [ch]: parsed }));
        } catch {
          // Ignore parse errors
        }
      });
    }

    es.onerror = () => {
      setConnected(false);
      setError('SSE connection lost');
      es.close();
      esRef.current = null;
      reconnectTimer.current = setTimeout(connect, 3000);
    };
  }, [channelsStr]);

  useEffect(() => {
    if (!enabled) return;
    connect();
    return () => {
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      if (esRef.current) {
        esRef.current.close();
        esRef.current = null;
      }
      setConnected(false);
    };
  }, [enabled, connect]);

  return { data, connected, error };
}
