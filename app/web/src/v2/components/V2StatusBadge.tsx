import type { ConvergenceStatus, V2ControlStatus } from '../api/contracts';

const statusMeta: Record<V2ControlStatus, { label: string; tone: string }> = {
  pending: { label: 'Pending', tone: 'pending' },
  running: { label: 'Running', tone: 'running' },
  failed: { label: 'Failed', tone: 'failed' },
  draining: { label: 'Draining', tone: 'draining' },
  offline: { label: 'Offline', tone: 'offline' },
  stopped: { label: 'Stopped', tone: 'offline' },
  unknown: { label: 'Unknown', tone: 'unknown' },
};

const convergenceMeta: Record<ConvergenceStatus, { label: string; tone: string }> = {
  converged: { label: 'Converged', tone: 'running' },
  pending: { label: 'Pending', tone: 'pending' },
  failed: { label: 'Failed', tone: 'failed' },
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

export function V2ConvergenceBadge({ status }: { status: ConvergenceStatus }) {
  const meta = convergenceMeta[status];
  return (
    <span className={`v2-status v2-status-${meta.tone}`}>
      <span className="v2-status-dot" />
      {meta.label}
    </span>
  );
}
