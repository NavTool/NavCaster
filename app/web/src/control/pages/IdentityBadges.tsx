import { Tag } from 'antd';
import type { AccountRole, AccountStatus, AccessAccountStatus } from '../../api/identity';

export function AccountStatusTag({ status }: { status: AccountStatus | AccessAccountStatus }) {
  const color = status === 'active' ? 'green' : status === 'disabled' ? 'orange' : status === 'deleted' ? 'red' : 'blue';
  return <Tag color={color}>{status}</Tag>;
}

export function RoleTag({ role }: { role: AccountRole }) {
  return <Tag color={role === 'admin' ? 'geekblue' : 'cyan'}>{role}</Tag>;
}

export function formatIdentityTime(value?: string | null) {
  if (!value) return '-';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '-';
  return new Intl.DateTimeFormat('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
  }).format(date);
}
