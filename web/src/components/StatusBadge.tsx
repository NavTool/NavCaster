import React from 'react';

interface StatusBadgeProps {
  online: number;
  total: number;
  label?: string;
}

const StatusBadge: React.FC<StatusBadgeProps> = ({ online, total, label }) => {
  const allOnline = online === total && total > 0;
  const color = allOnline ? '#52c41a' : total === 0 ? '#6b7194' : '#faad14';

  return (
    <span style={{
      display: 'inline-flex', alignItems: 'center', gap: 6,
      padding: '2px 10px', borderRadius: 12,
      background: `${color}18`, border: `1px solid ${color}40`,
      fontSize: 12, color, fontWeight: 500, lineHeight: '20px',
    }}>
      <span style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
      {label && <span>{label}</span>}
      <span>{online}/{total}</span>
    </span>
  );
};

export default StatusBadge;
