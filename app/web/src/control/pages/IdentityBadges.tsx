import { Tag } from 'antd';
import type { AccountRole, AccountStatus, AccessAccountStatus } from '../../api/identity';
import { accountRoleLabel, accountStatusLabel } from '../labels';

export function AccountStatusTag({ status }: { status: AccountStatus | AccessAccountStatus }) {
  const color = status === 'active' ? 'green' : status === 'disabled' ? 'orange' : status === 'deleted' ? 'red' : 'blue';
  return <Tag color={color}>{accountStatusLabel(status)}</Tag>;
}

export function RoleTag({ role }: { role: AccountRole }) {
  return <Tag color={role === 'admin' ? 'geekblue' : 'cyan'}>{accountRoleLabel(role)}</Tag>;
}
