import type { V2ControlStatus } from '../api/contracts';

const statusMeta: Record<V2ControlStatus, { label: string; tone: string }> = {
  pending: { label: 'Pending', tone: 'pending' },
  running: { label: 'Running', tone: 'running' },
  failed: { label: 'Failed', tone: 'failed' },
  draining: { label: 'Draining', tone: 'draining' },
  offline: { label: 'Offline', tone: 'offline' },
};

export function V2StatusBadge({ status }: { status: V2ControlStatus }) {
  const meta = statusMeta[status];
  return (
    <span className={`v2-status v2-status-${meta.tone}`}>
      <span className="v2-status-dot" />
      {meta.label}
    </span>
  );
}
