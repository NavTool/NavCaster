import type { ReactNode } from 'react';

export function MetricCard({
  label,
  value,
  detail,
  icon,
}: {
  label: string;
  value: ReactNode;
  detail?: string;
  icon?: ReactNode;
}) {
  return (
    <section className="control-metric-card">
      <div>
        <div className="control-metric-label">{label}</div>
        <div className="control-metric-value">{value}</div>
        {detail ? <div className="control-metric-detail">{detail}</div> : null}
      </div>
      {icon ? <div className="control-metric-icon">{icon}</div> : null}
    </section>
  );
}
