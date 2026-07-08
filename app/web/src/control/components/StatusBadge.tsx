import type { ConvergenceStatus, ControlStatus } from '../../api/contracts';
import { controlStatusLabel, convergenceStatusLabel } from '../labels';

const statusMeta: Record<ControlStatus, { label: string; tone: string }> = {
  pending: { label: controlStatusLabel('pending'), tone: 'pending' },
  running: { label: controlStatusLabel('running'), tone: 'running' },
  failed: { label: controlStatusLabel('failed'), tone: 'failed' },
  draining: { label: controlStatusLabel('draining'), tone: 'draining' },
  offline: { label: controlStatusLabel('offline'), tone: 'offline' },
  stopped: { label: controlStatusLabel('stopped'), tone: 'offline' },
  unknown: { label: controlStatusLabel('unknown'), tone: 'unknown' },
};

const convergenceMeta: Record<ConvergenceStatus, { label: string; tone: string }> = {
  converged: { label: convergenceStatusLabel('converged'), tone: 'running' },
  pending: { label: convergenceStatusLabel('pending'), tone: 'pending' },
  failed: { label: convergenceStatusLabel('failed'), tone: 'failed' },
};

export function StatusBadge({ status }: { status: ControlStatus }) {
  const meta = statusMeta[status];
  return (
    <span className={`control-status control-status-${meta.tone}`}>
      <span className="control-status-dot" />
      {meta.label}
    </span>
  );
}

export function ConvergenceBadge({ status }: { status: ConvergenceStatus }) {
  const meta = convergenceMeta[status];
  return (
    <span className={`control-status control-status-${meta.tone}`}>
      <span className="control-status-dot" />
      {meta.label}
    </span>
  );
}
