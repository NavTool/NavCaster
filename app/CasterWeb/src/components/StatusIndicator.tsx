import React from 'react';

type StatusType = 'online' | 'offline' | 'degraded' | 'connected' | 'disconnected' | 'active' | 'inactive' | 'running' | 'paused' | 'stopped' | 'error';

interface StatusIndicatorProps {
  status: StatusType;
  pulse?: boolean;
  size?: 'sm' | 'md' | 'lg';
  label?: string;
}

const colorMap: Record<string, string> = {
  online: '#52c41a',
  connected: '#52c41a',
  active: '#52c41a',
  running: '#52c41a',
  degraded: '#faad14',
  paused: '#faad14',
  offline: '#ff4d4f',
  disconnected: '#ff4d4f',
  inactive: '#6b7194',
  stopped: '#6b7194',
  error: '#ff4d4f',
};

const sizeMap = { sm: 6, md: 8, lg: 12 };

const StatusIndicator: React.FC<StatusIndicatorProps> = ({ status, pulse = false, size = 'md', label }) => {
  const color = colorMap[status] || '#6b7194';
  const dotSize = sizeMap[size];
  const shouldPulse = pulse && ['online', 'connected', 'active', 'running'].includes(status);

  return (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
      <span style={{ position: 'relative', display: 'inline-flex' }}>
        <span style={{
          width: dotSize,
          height: dotSize,
          borderRadius: '50%',
          background: color,
        }} />
        {shouldPulse && (
          <span style={{
            position: 'absolute',
            inset: 0,
            borderRadius: '50%',
            background: color,
            animation: 'status-pulse 2s ease-out infinite',
          }} />
        )}
      </span>
      {label && <span style={{ fontSize: 13, color: '#9da1b8', textTransform: 'capitalize' }}>{label}</span>}
    </span>
  );
};

export default StatusIndicator;
