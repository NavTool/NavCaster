import api from './client';
import type {
  AccessAccountCreateInput,
  AccessAccountRecord,
  AccessAccountUpdateInput,
  AccountGroupGrant,
  AuthSessionSubject,
  DataPushConfig,
  DataPushJob,
  DataPushMaintenanceConfig,
  DataPushMaintenanceResult,
  BillingUsageEntry,
  DataPushUsage,
  HashRecord,
  MountPointGroup,
  MountPointRecord,
  OperationsAccount,
  OperationsMonitor,
  RedeemCodeRecord,
  RedeemRedemptionRecord,
  RoleDashboard,
  StationRecord,
  SubscriptionRecord,
  SupplierEarningsSummary,
  SupplierSettlementRecord,
  SupplierSupplyUsage,
} from './types';

export async function getSession(): Promise<AuthSessionSubject> {
  const { data } = await api.get('/api/v1/auth/session');
  return data as AuthSessionSubject;
}

export const adminApi = {
  async operationsMonitor(period?: string): Promise<OperationsMonitor> {
    const { data } = await api.get('/api/v1/admin/operations-monitor', { params: period ? { period } : undefined });
    return data as OperationsMonitor;
  },
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
  async createSubscription(body: Partial<SubscriptionRecord> & { subscription_id: string; account_id: string; group_ids: string[] }) {
    const { data } = await api.post('/api/v1/admin/subscriptions', body);
    return data as SubscriptionRecord;
  },
  async updateSubscription(subscriptionId: string, body: Partial<SubscriptionRecord>) {
    const { data } = await api.put(`/api/v1/admin/subscriptions/${encodeURIComponent(subscriptionId)}`, body);
    return data as SubscriptionRecord;
  },
  async redeemCodes(): Promise<HashRecord<RedeemCodeRecord>> {
    const { data } = await api.get('/api/v1/admin/redeem-codes');
    return data;
  },
  async createRedeemCode(body: Partial<RedeemCodeRecord> & { code: string; amount_cents: number }) {
    const { data } = await api.post('/api/v1/admin/redeem-codes', body);
    return data as RedeemCodeRecord;
  },
  async redeemCode(code: string, body: { account_id: string; period?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/admin/redeem-codes/${encodeURIComponent(code)}/redeem`, body);
    return data as RedeemRedemptionRecord;
  },
  async stations(): Promise<HashRecord<StationRecord>> {
    const { data } = await api.get('/api/v1/admin/stations');
    return data;
  },
  async usage(period?: string): Promise<HashRecord<BillingUsageEntry>> {
    const { data } = await api.get('/api/v1/admin/usage', { params: period ? { period } : undefined });
    return data;
  },
  async dataPushConfigs(): Promise<HashRecord<DataPushConfig>> {
    const { data } = await api.get('/api/v1/admin/data-push-configs');
    return data;
  },
  async createDataPushConfig(body: Partial<DataPushConfig> & { config_id: string; name: string; target_mountpoint: string }) {
    const { data } = await api.post('/api/v1/admin/data-push-configs', body);
    return data as DataPushConfig;
  },
  async updateDataPushConfig(configId: string, body: Partial<DataPushConfig>) {
    const { data } = await api.put(`/api/v1/admin/data-push-configs/${encodeURIComponent(configId)}`, body);
    return data as DataPushConfig;
  },
  async deleteDataPushConfig(configId: string) {
    const { data } = await api.delete(`/api/v1/admin/data-push-configs/${encodeURIComponent(configId)}`);
    return data as DataPushConfig;
  },
  async dataPushJobs(period?: string): Promise<HashRecord<DataPushJob>> {
    const { data } = await api.get('/api/v1/admin/data-push-jobs', { params: period ? { period } : undefined });
    return data;
  },
  async controlDataPushJob(jobId: string, body: { action: 'cancel' | 'retry' | 'mark_failed' | 'mark_completed'; period?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/admin/data-push-jobs/${encodeURIComponent(jobId)}/control`, body, { params: body.period ? { period: body.period } : undefined });
    return data as DataPushJob;
  },
  async reconcileDataPushJob(jobId: string, body: { period?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/admin/data-push-jobs/${encodeURIComponent(jobId)}/reconcile`, body, { params: body.period ? { period: body.period } : undefined });
    return data as DataPushJob;
  },
  async reconcileDataPushJobs(period?: string) {
    const { data } = await api.post('/api/v1/admin/data-push-jobs', {}, { params: { action: 'reconcile', ...(period ? { period } : {}) } });
    return data as { period: string; updated_count: number; items: HashRecord<DataPushJob> };
  },
  async dataPushMaintenanceConfig(): Promise<DataPushMaintenanceConfig> {
    const { data } = await api.get('/api/v1/admin/data-push-maintenance');
    return data as DataPushMaintenanceConfig;
  },
  async updateDataPushMaintenanceConfig(body: Partial<DataPushMaintenanceConfig>) {
    const { data } = await api.put('/api/v1/admin/data-push-maintenance', body);
    return data as DataPushMaintenanceConfig;
  },
  async runDataPushMaintenance(body: { period?: string; unhealthy_after_seconds?: number }) {
    const { data } = await api.post('/api/v1/admin/data-push-maintenance', body, {
      params: { action: 'run', ...(body.period ? { period: body.period } : {}) },
    });
    return data as DataPushMaintenanceResult;
  },
  async dataPushUsage(period?: string): Promise<HashRecord<DataPushUsage>> {
    const { data } = await api.get('/api/v1/admin/data-push-usage', { params: period ? { period } : undefined });
    return data;
  },
  async supplyUsage(period?: string): Promise<HashRecord<SupplierSupplyUsage>> {
    const { data } = await api.get('/api/v1/admin/supply-usage', { params: period ? { period } : undefined });
    return data;
  },
  async supplierSettlements(period?: string, supplierAccountId?: string): Promise<HashRecord<SupplierSettlementRecord>> {
    const params = {
      ...(period ? { period } : {}),
      ...(supplierAccountId ? { supplier_account_id: supplierAccountId } : {}),
    };
    const { data } = await api.get('/api/v1/admin/supplier-settlements', { params: Object.keys(params).length ? params : undefined });
    return data;
  },
  async createSupplierSettlement(body: { supplier_account_id: string; period?: string; usage_ids?: string[]; operator_note?: string; external_ref?: string }) {
    const { data } = await api.post('/api/v1/admin/supplier-settlements', body);
    return data as SupplierSettlementRecord;
  },
  async supplierSettlement(settlementId: string, period?: string, supplierAccountId?: string) {
    const params = {
      ...(period ? { period } : {}),
      ...(supplierAccountId ? { supplier_account_id: supplierAccountId } : {}),
    };
    const { data } = await api.get(`/api/v1/admin/supplier-settlements/${encodeURIComponent(settlementId)}`, { params: Object.keys(params).length ? params : undefined });
    return data as SupplierSettlementRecord;
  },
  async updateSupplierSettlementPayment(settlementId: string, body: { supplier_account_id: string; period: string; status: 'paid' | 'payment_failed' | 'cancelled'; payment_method?: string; payment_ref?: string; payment_note?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/admin/supplier-settlements/${encodeURIComponent(settlementId)}/payment`, body);
    return data as SupplierSettlementRecord;
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
  async subscriptions(): Promise<HashRecord<SubscriptionRecord>> {
    const { data } = await api.get('/api/v1/me/subscriptions');
    return data;
  },
  async redeemRedemptions(): Promise<HashRecord<RedeemRedemptionRecord>> {
    const { data } = await api.get('/api/v1/me/redeem-redemptions');
    return data;
  },
  async dataPushUsage(period?: string): Promise<HashRecord<DataPushUsage>> {
    const { data } = await api.get('/api/v1/me/data-push', { params: period ? { period } : undefined });
    return data;
  },
  async dataPushConfigs(): Promise<HashRecord<DataPushConfig>> {
    const { data } = await api.get('/api/v1/me/data-push/configs');
    return data;
  },
  async dataPushJobs(period?: string): Promise<HashRecord<DataPushJob>> {
    const { data } = await api.get('/api/v1/me/data-push/jobs', { params: period ? { period } : undefined });
    return data;
  },
  async createDataPushJob(body: { config_id: string; used_seconds: number; period?: string; operator_note?: string }) {
    const { data } = await api.post('/api/v1/me/data-push/jobs', body);
    return data as DataPushJob;
  },
  async controlDataPushJob(jobId: string, body: { action: 'cancel' | 'retry'; period?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/me/data-push/jobs/${encodeURIComponent(jobId)}/control`, body, { params: body.period ? { period: body.period } : undefined });
    return data as DataPushJob;
  },
  async reconcileDataPushJob(jobId: string, body: { period?: string; operator_note?: string }) {
    const { data } = await api.post(`/api/v1/me/data-push/jobs/${encodeURIComponent(jobId)}/reconcile`, body, { params: body.period ? { period: body.period } : undefined });
    return data as DataPushJob;
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
  async settlements(period?: string): Promise<HashRecord<SupplierSettlementRecord>> {
    const { data } = await api.get('/api/v1/supplier/settlements', { params: period ? { period } : undefined });
    return data;
  },
};
