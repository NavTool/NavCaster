import api from './client';
import type {
  AccessAccountCreateInput,
  AccessAccountRecord,
  AccessAccountUpdateInput,
  AccountGroupGrant,
  AuthSessionSubject,
  BillingUsageEntry,
  DataPushUsage,
  HashRecord,
  MountPointGroup,
  MountPointRecord,
  OperationsAccount,
  RoleDashboard,
  StationRecord,
  SubscriptionRecord,
  SupplierEarningsSummary,
  SupplierSupplyUsage,
} from './types';

export async function getSession(): Promise<AuthSessionSubject> {
  const { data } = await api.get('/api/v1/auth/session');
  return data as AuthSessionSubject;
}

export const adminApi = {
  async accounts(): Promise<HashRecord<OperationsAccount>> {
    const { data } = await api.get('/api/v1/admin/accounts');
    return data;
  },
  async createAccount(body: Partial<OperationsAccount> & { account_id: string; username: string; role: string; password?: string }) {
    const { data } = await api.post('/api/v1/admin/accounts', body);
    return data as OperationsAccount;
  },
  async updateAccount(accountId: string, body: Partial<OperationsAccount>) {
    const { data } = await api.put(`/api/v1/admin/accounts/${encodeURIComponent(accountId)}`, body);
    return data as OperationsAccount;
  },
  async grantGroup(accountId: string, groupId: string) {
    const { data } = await api.put(`/api/v1/admin/accounts/${encodeURIComponent(accountId)}/group-grants`, { group_id: groupId });
    return data as AccountGroupGrant;
  },
  async addBalanceAdjustment(accountId: string, body: { ledger_id: string; delta_cents: number; period?: string; reason?: string }) {
    const { data } = await api.post(`/api/v1/admin/accounts/${encodeURIComponent(accountId)}/balance-adjustments`, body);
    return data;
  },
  async accessAccounts(): Promise<HashRecord<AccessAccountRecord>> {
    const { data } = await api.get('/api/v1/admin/access-accounts');
    return data;
  },
  async mountPointGroups(): Promise<HashRecord<MountPointGroup>> {
    const { data } = await api.get('/api/v1/admin/mount-point-groups');
    return data;
  },
  async createMountPointGroup(body: Partial<MountPointGroup> & { group_id: string; name: string }) {
    const { data } = await api.post('/api/v1/admin/mount-point-groups', body);
    return data as MountPointGroup;
  },
  async addGroupMember(groupId: string, mountpoint: string) {
    const { data } = await api.put(`/api/v1/admin/mount-point-groups/${encodeURIComponent(groupId)}/members`, { mountpoint });
    return data;
  },
  async mountPoints(): Promise<HashRecord<MountPointRecord>> {
    const { data } = await api.get('/api/v1/admin/mount-points');
    return data;
  },
  async upsertMountPoint(mountpoint: string, body: Partial<MountPointRecord>) {
    const { data } = await api.put(`/api/v1/admin/mount-points/${encodeURIComponent(mountpoint)}`, body);
    return data as MountPointRecord;
  },
  async subscriptions(): Promise<HashRecord<SubscriptionRecord>> {
    const { data } = await api.get('/api/v1/admin/subscriptions');
    return data;
  },
  async stations(): Promise<HashRecord<StationRecord>> {
    const { data } = await api.get('/api/v1/admin/stations');
    return data;
  },
  async usage(period?: string): Promise<HashRecord<BillingUsageEntry>> {
    const { data } = await api.get('/api/v1/admin/usage', { params: period ? { period } : undefined });
    return data;
  },
  async dataPushUsage(period?: string): Promise<HashRecord<DataPushUsage>> {
    const { data } = await api.get('/api/v1/admin/data-push-usage', { params: period ? { period } : undefined });
    return data;
  },
  async supplyUsage(period?: string): Promise<HashRecord<SupplierSupplyUsage>> {
    const { data } = await api.get('/api/v1/admin/supply-usage', { params: period ? { period } : undefined });
    return data;
  },
};

function accessApi(scope: 'me' | 'supplier') {
  const base = scope === 'me' ? '/api/v1/me' : '/api/v1/supplier';
  return {
    async profile(): Promise<OperationsAccount> {
      const { data } = await api.get(`${base}/profile`);
      return data;
    },
    async dashboard(): Promise<RoleDashboard> {
      const { data } = await api.get(`${base}/dashboard`);
      return data;
    },
    async accessAccounts(): Promise<HashRecord<AccessAccountRecord>> {
      const { data } = await api.get(`${base}/access-accounts`);
      return data;
    },
    async createAccessAccount(body: AccessAccountCreateInput) {
      const { data } = await api.post(`${base}/access-accounts`, body);
      return data as AccessAccountRecord;
    },
    async updateAccessAccount(accessAccountId: string, body: AccessAccountUpdateInput) {
      const { data } = await api.put(`${base}/access-accounts/${encodeURIComponent(accessAccountId)}`, body);
      return data as AccessAccountRecord;
    },
    async updateAccessAccountPassword(accessAccountId: string, password: string) {
      const { data } = await api.put(`${base}/access-accounts/${encodeURIComponent(accessAccountId)}/password`, { password });
      return data as AccessAccountRecord;
    },
    async deleteAccessAccount(accessAccountId: string) {
      const { data } = await api.delete(`${base}/access-accounts/${encodeURIComponent(accessAccountId)}`);
      return data as AccessAccountRecord;
    },
  };
}

export const meApi = {
  ...accessApi('me'),
  async allowedGroups(): Promise<HashRecord<AccountGroupGrant>> {
    const { data } = await api.get('/api/v1/me/allowed-groups');
    return data;
  },
  async mountPoints(): Promise<HashRecord<MountPointRecord>> {
    const { data } = await api.get('/api/v1/me/mount-points');
    return data;
  },
  async usage(period?: string): Promise<HashRecord<BillingUsageEntry>> {
    const { data } = await api.get('/api/v1/me/usage', { params: period ? { period } : undefined });
    return data;
  },
  async dataPushUsage(period?: string): Promise<HashRecord<DataPushUsage>> {
    const { data } = await api.get('/api/v1/me/data-push', { params: period ? { period } : undefined });
    return data;
  },
};

export const supplierApi = {
  ...accessApi('supplier'),
  async stations(): Promise<HashRecord<StationRecord>> {
    const { data } = await api.get('/api/v1/supplier/stations');
    return data;
  },
  async supplyUsage(period?: string): Promise<HashRecord<SupplierSupplyUsage>> {
    const { data } = await api.get('/api/v1/supplier/supply-usage', { params: period ? { period } : undefined });
    return data;
  },
  async earnings(period?: string): Promise<SupplierEarningsSummary> {
    const { data } = await api.get('/api/v1/supplier/earnings', { params: period ? { period } : undefined });
    return data;
  },
};
