import type { ReactNode } from 'react';

export function V2MetricCard({
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
    <section className="v2-metric-card">
      <div>
        <div className="v2-metric-label">{label}</div>
        <div className="v2-metric-value">{value}</div>
        {detail ? <div className="v2-metric-detail">{detail}</div> : null}
      </div>
      {icon ? <div className="v2-metric-icon">{icon}</div> : null}
    </section>
  );
}
