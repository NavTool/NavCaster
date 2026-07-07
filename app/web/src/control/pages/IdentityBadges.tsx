import { Tag } from 'antd';
import type { AccountRole, AccountStatus, AccessAccountStatus } from '../../api/identity';

export function AccountStatusTag({ status }: { status: AccountStatus | AccessAccountStatus }) {
  const color = status === 'active' ? 'green' : status === 'disabled' ? 'orange' : status === 'deleted' ? 'red' : 'blue';
  return <Tag color={color}>{status}</Tag>;
}

export function RoleTag({ role }: { role: AccountRole }) {
  return <Tag color={role === 'admin' ? 'geekblue' : 'cyan'}>{role}</Tag>;
}
