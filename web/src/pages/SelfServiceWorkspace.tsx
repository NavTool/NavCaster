import React, { useMemo, useState } from 'react';
import {
  Button,
  Col,
  Descriptions,
  Form,
  Input,
  InputNumber,
  Modal,
  Row,
  Select,
  Space,
  Table,
  Tag,
  message,
} from 'antd';
import {
  BranchesOutlined,
  CloudServerOutlined,
  LockOutlined,
  PlusOutlined,
  ProfileOutlined,
  SwapOutlined,
  WalletOutlined,
} from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import PageContainer from '../components/PageContainer';
import MetricCard from '../components/MetricCard';
import { usePolling } from '../hooks/usePolling';
import { meApi, supplierApi } from '../api/operations';
import { useRoleSession } from '../role';
import type {
  AccessAccountCreateInput,
  AccessAccountRecord,
  AccountGroupGrant,
  BillingUsageEntry,
  DataPushConfig,
  DataPushJob,
  DataPushUsage,
  HashRecord,
  MountPointRecord,
  OperationsAccount,
  RedeemRedemptionRecord,
  RoleDashboard,
  StationRecord,
  SubscriptionRecord,
  SupplierEarningsSummary,
  SupplierSettlementRecord,
  SupplierSupplyUsage,
} from '../api/types';
import { currentPeriod, formatCents, formatDuration, getLocalTime } from '../utils/format';

type SelfScope = 'me' | 'supplier';
type UserView = 'dashboard' | 'profile' | 'access-accounts' | 'groups' | 'mount-points' | 'usage' | 'subscriptions' | 'redeem-redemptions' | 'data-push';
type SupplierView = 'dashboard' | 'profile' | 'access-accounts' | 'stations' | 'supply-usage' | 'settlements' | 'earnings';

interface SelfServiceWorkspaceProps {
  scope: SelfScope;
  view: UserView | SupplierView;
}

function rowsFromHash<T extends object>(records: HashRecord<T> | null | undefined): Array<T & { key: string }> {
  return Object.entries(records ?? {}).map(([key, value]) => ({ key, ...value }));
}

function statusTag(status?: string) {
  const color = status === 'active' ? 'green' : status === 'disabled' ? 'orange' : status === 'deleted' ? 'red' : 'default';
  return <Tag color={color}>{status || '-'}</Tag>;
}

function settlementStatusTag(status?: string) {
  const color = status === 'paid' || status === 'settled'
    ? 'green'
    : status === 'pending_payment'
      ? 'gold'
      : status === 'payment_failed'
        ? 'red'
        : 'default';
  return <Tag color={color}>{status || 'pending_payment'}</Tag>;
}

function dataPushExecutionTag(mode?: string) {
  return <Tag color={mode === 'relay_push' ? 'blue' : 'default'}>{mode || 'ledger_only'}</Tag>;
}

function dataPushJobStatusTag(status?: string) {
  const color = status === 'completed'
    ? 'green'
    : status === 'running'
      ? 'blue'
      : status === 'queued'
        ? 'gold'
        : status === 'failed'
          ? 'red'
          : 'default';
  return <Tag color={color}>{status || '-'}</Tag>;
}

function kindLabel(scope: SelfScope): string {
  return scope === 'supplier' ? 'supplier_station' : 'user_client';
}

function scopeApi(scope: SelfScope) {
  return scope === 'supplier' ? supplierApi : meApi;
}

const SelfServiceWorkspace: React.FC<SelfServiceWorkspaceProps> = ({ scope, view }) => {
  const session = useRoleSession();
  const api = scopeApi(scope);
  const [form] = Form.useForm();
  const [passwordForm] = Form.useForm();
  const [dataPushJobForm] = Form.useForm();
  const [editing, setEditing] = useState<AccessAccountRecord | null>(null);
  const [modalOpen, setModalOpen] = useState(false);
  const [passwordTarget, setPasswordTarget] = useState<AccessAccountRecord | null>(null);
  const [dataPushJobOpen, setDataPushJobOpen] = useState(false);

  const dashboardQuery = usePolling(() => api.dashboard(), 5000, view === 'dashboard');
  const profileQuery = usePolling(() => api.profile(), 5000, view === 'dashboard' || view === 'profile');
  const accessQuery = usePolling(() => api.accessAccounts(), 5000, view === 'dashboard' || view === 'access-accounts');
  const groupsQuery = usePolling(() => meApi.allowedGroups(), 5000, scope === 'me' && (view === 'dashboard' || view === 'groups' || view === 'access-accounts'));
  const mountsQuery = usePolling(() => meApi.mountPoints(), 5000, scope === 'me' && (view === 'dashboard' || view === 'mount-points'));
  const usageQuery = usePolling(() => meApi.usage(currentPeriod()), 5000, scope === 'me' && (view === 'dashboard' || view === 'usage'));
  const subscriptionQuery = usePolling(() => meApi.subscriptions(), 5000, scope === 'me' && (view === 'dashboard' || view === 'subscriptions'));
  const redemptionQuery = usePolling(() => meApi.redeemRedemptions(), 5000, scope === 'me' && (view === 'dashboard' || view === 'redeem-redemptions'));
  const dataPushConfigQuery = usePolling(() => meApi.dataPushConfigs(), 5000, scope === 'me' && (view === 'dashboard' || view === 'data-push'));
  const dataPushJobQuery = usePolling(() => meApi.dataPushJobs(currentPeriod()), 5000, scope === 'me' && (view === 'dashboard' || view === 'data-push'));
  const dataPushQuery = usePolling(() => meApi.dataPushUsage(currentPeriod()), 5000, scope === 'me' && (view === 'dashboard' || view === 'data-push'));
  const stationsQuery = usePolling(() => supplierApi.stations(), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'stations'));
  const supplyQuery = usePolling(() => supplierApi.supplyUsage(currentPeriod()), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'supply-usage' || view === 'earnings'));
  const settlementQuery = usePolling(() => supplierApi.settlements(currentPeriod()), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'settlements' || view === 'earnings'));
  const earningsQuery = usePolling(() => supplierApi.earnings(currentPeriod()), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'earnings'));

  const accessRows = useMemo(() => rowsFromHash(accessQuery.data), [accessQuery.data]);
  const groupRows = useMemo(() => rowsFromHash(groupsQuery.data), [groupsQuery.data]);
  const mountRows = useMemo(() => rowsFromHash(mountsQuery.data), [mountsQuery.data]);
  const usageRows = useMemo(() => rowsFromHash(usageQuery.data), [usageQuery.data]);
  const subscriptionRows = useMemo(() => rowsFromHash(subscriptionQuery.data), [subscriptionQuery.data]);
  const redemptionRows = useMemo(() => rowsFromHash(redemptionQuery.data), [redemptionQuery.data]);
  const dataPushConfigRows = useMemo(() => rowsFromHash(dataPushConfigQuery.data), [dataPushConfigQuery.data]);
  const dataPushJobRows = useMemo(() => rowsFromHash(dataPushJobQuery.data), [dataPushJobQuery.data]);
  const dataPushRows = useMemo(() => rowsFromHash(dataPushQuery.data), [dataPushQuery.data]);
  const stationRows = useMemo(() => rowsFromHash(stationsQuery.data), [stationsQuery.data]);
  const supplyRows = useMemo(() => rowsFromHash(supplyQuery.data), [supplyQuery.data]);
  const settlementRows = useMemo(() => rowsFromHash(settlementQuery.data), [settlementQuery.data]);

  const groupOptions = groupRows.map((grant) => ({
    value: grant.group_id,
    label: grant.group?.name ? `${grant.group.name} (${grant.group_id})` : grant.group_id,
  }));
  const dataPushConfigOptions = dataPushConfigRows.map((config) => ({
    value: config.config_id,
    label: `${config.name || config.config_id} -> ${config.target_mountpoint} (${config.execution_mode || 'ledger_only'})`,
  }));

  const openCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      access_account_id: `aacc_${Date.now()}`,
      status: 'active',
      concurrency_limit: 1,
      expire_time: 0,
    });
    setModalOpen(true);
  };

  const openEdit = (record: AccessAccountRecord) => {
    setEditing(record);
    form.resetFields();
    form.setFieldsValue({
      mount_point_group_id: record.mount_point_group_id,
      status: record.status,
      concurrency_limit: record.concurrency_limit,
      expire_time: record.expire_time ?? 0,
      private_remark: record.private_remark,
    });
    setModalOpen(true);
  };

  const submitAccessAccount = async () => {
    const values = await form.validateFields();
    if (editing) {
      const body = {
        mount_point_group_id: values.mount_point_group_id,
        status: values.status,
        concurrency_limit: values.concurrency_limit,
        expire_time: values.expire_time,
        private_remark: values.private_remark,
      };
      await api.updateAccessAccount(editing.access_account_id, body);
      message.success('接入账号已更新');
    } else {
      const body: AccessAccountCreateInput = {
        access_account_id: values.access_account_id,
        username: values.username,
        password: values.password,
        mount_point_group_id: values.mount_point_group_id,
        status: values.status,
        concurrency_limit: values.concurrency_limit,
        expire_time: values.expire_time,
        private_remark: values.private_remark,
      };
      await api.createAccessAccount(body);
      message.success('接入账号已创建');
    }
    setModalOpen(false);
    accessQuery.refresh();
  };

  const submitPassword = async () => {
    if (!passwordTarget) return;
    const values = await passwordForm.validateFields();
    await api.updateAccessAccountPassword(passwordTarget.access_account_id, values.password);
    message.success('密码已更新');
    setPasswordTarget(null);
  };

  const deleteAccessAccount = async (record: AccessAccountRecord) => {
    await api.deleteAccessAccount(record.access_account_id);
    message.success('接入账号已删除');
    accessQuery.refresh();
  };

  const openDataPushJob = () => {
    dataPushJobForm.resetFields();
    dataPushJobForm.setFieldsValue({
      period: currentPeriod(),
      used_seconds: 3600,
    });
    setDataPushJobOpen(true);
  };

  const submitDataPushJob = async () => {
    const values = await dataPushJobForm.validateFields();
    await meApi.createDataPushJob(values);
    message.success('推送任务已完成');
    setDataPushJobOpen(false);
    dataPushJobQuery.refresh();
    dataPushQuery.refresh();
    profileQuery.refresh();
    dashboardQuery.refresh();
  };

  const controlDataPushJob = async (record: DataPushJob, action: 'cancel' | 'retry') => {
    await meApi.controlDataPushJob(record.job_id, {
      action,
      period: record.period || currentPeriod(),
      operator_note: `self ${action}`,
    });
    message.success('推送任务状态已更新');
    dataPushJobQuery.refresh();
    dashboardQuery.refresh();
  };

  const reconcileDataPushJob = async (record: DataPushJob) => {
    await meApi.reconcileDataPushJob(record.job_id, {
      period: record.period || currentPeriod(),
      operator_note: 'self reconcile',
    });
    message.success('推送任务运行态已同步');
    dataPushJobQuery.refresh();
    dashboardQuery.refresh();
  };

  const accessColumns: ColumnsType<AccessAccountRecord & { key: string }> = [
    { title: 'AccessAccount ID', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '接入用户名', dataIndex: 'username', key: 'username', width: 150 },
    { title: '类型', key: 'kind', width: 140, render: (_, row) => <Tag color={scope === 'supplier' ? 'blue' : 'green'}>{row.kind || kindLabel(scope)}</Tag> },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '分组', dataIndex: 'mount_point_group_id', key: 'mount_point_group_id', width: 180 },
    { title: '并发', dataIndex: 'concurrency_limit', key: 'concurrency_limit', width: 80 },
    { title: '过期时间', key: 'expire_time', width: 170, render: (_, row) => row.expire_time ? getLocalTime(row.expire_time) : '长期' },
    { title: '备注', dataIndex: 'private_remark', key: 'private_remark', ellipsis: true, render: (value) => value || '-' },
    {
      title: '操作',
      key: 'actions',
      fixed: 'right',
      width: 210,
      render: (_, row) => (
        <Space>
          <Button size="small" onClick={() => openEdit(row)}>编辑</Button>
          <Button size="small" onClick={() => { passwordForm.resetFields(); setPasswordTarget(row); }}>密码</Button>
          <Button size="small" danger onClick={() => deleteAccessAccount(row)}>删除</Button>
        </Space>
      ),
    },
  ];

  const groupColumns: ColumnsType<AccountGroupGrant & { key: string }> = [
    { title: 'Group ID', dataIndex: 'group_id', key: 'group_id', width: 180 },
    { title: '名称', key: 'name', width: 180, render: (_, row) => row.group?.name || '-' },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '倍率', key: 'billing_multiplier', width: 100, render: (_, row) => row.group?.billing_multiplier ?? '-' },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
  ];

  const mountColumns: ColumnsType<MountPointRecord & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 180 },
    { title: '分组', dataIndex: 'group_id', key: 'group_id', width: 180 },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '小时价格', key: 'hourly_price_cents', width: 120, render: (_, row) => formatCents(row.hourly_price_cents) },
  ];

  const usageColumns: ColumnsType<BillingUsageEntry & { key: string }> = [
    { title: 'Billing ID', dataIndex: 'billing_id', key: 'billing_id', width: 230 },
    { title: 'AccessAccount', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 150 },
    { title: '模式', dataIndex: 'billing_mode', key: 'billing_mode', width: 110, render: (value) => value || 'payg' },
    { title: '订阅', dataIndex: 'subscription_id', key: 'subscription_id', width: 170, render: (value) => value || '-' },
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const subscriptionColumns: ColumnsType<SubscriptionRecord & { key: string }> = [
    { title: 'Subscription ID', dataIndex: 'subscription_id', key: 'subscription_id', width: 210 },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '分组', key: 'group_ids', width: 220, render: (_, row) => (row.group_ids || []).join(', ') || '-' },
    { title: '开始', key: 'start_time', width: 170, render: (_, row) => row.start_time ? getLocalTime(row.start_time) : '立即' },
    { title: '过期', key: 'expire_time', width: 170, render: (_, row) => row.expire_time ? getLocalTime(row.expire_time) : '长期' },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
  ];

  const redemptionColumns: ColumnsType<RedeemRedemptionRecord & { key: string }> = [
    { title: 'Redemption ID', dataIndex: 'redemption_id', key: 'redemption_id', width: 240 },
    { title: 'Code', dataIndex: 'code', key: 'code', width: 160 },
    { title: '金额', key: 'amount_cents', width: 120, render: (_, row) => formatCents(row.amount_cents) },
    { title: '入账后余额', key: 'balance_after_cents', width: 140, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '账本', dataIndex: 'ledger_id', key: 'ledger_id', width: 220 },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const dataPushColumns: ColumnsType<DataPushUsage & { key: string }> = [
    { title: 'Usage ID', dataIndex: 'usage_id', key: 'usage_id', width: 230 },
    { title: '配置', dataIndex: 'config_id', key: 'config_id', width: 160, render: (value) => value || '-' },
    { title: '任务', dataIndex: 'job_id', key: 'job_id', width: 220, render: (value) => value || '-' },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '扣后余额', key: 'balance_after_cents', width: 120, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const dataPushConfigColumns: ColumnsType<DataPushConfig & { key: string }> = [
    { title: 'Config ID', dataIndex: 'config_id', key: 'config_id', width: 180 },
    { title: '名称', dataIndex: 'name', key: 'name', width: 180 },
    { title: '执行模式', key: 'execution_mode', width: 130, render: (_, row) => dataPushExecutionTag(row.execution_mode) },
    { title: '源挂载点', dataIndex: 'source_mountpoint', key: 'source_mountpoint', width: 150, render: (value) => value || '-' },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: '小时价格', key: 'fixed_hourly_price_cents', width: 120, render: (_, row) => formatCents(row.fixed_hourly_price_cents) },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '描述', dataIndex: 'description', key: 'description', ellipsis: true, render: (value) => value || '-' },
  ];

  const dataPushJobColumns: ColumnsType<DataPushJob & { key: string }> = [
    { title: 'Job ID', dataIndex: 'job_id', key: 'job_id', width: 260 },
    { title: '配置', dataIndex: 'config_id', key: 'config_id', width: 160 },
    { title: '执行模式', key: 'execution_mode', width: 130, render: (_, row) => dataPushExecutionTag(row.execution_mode) },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: 'Relay UID', dataIndex: 'relay_uid', key: 'relay_uid', width: 250, render: (value) => value || '-' },
    { title: 'Relay 状态', dataIndex: 'relay_status', key: 'relay_status', width: 120, render: (value) => <Tag color={value === 'running' ? 'blue' : value === 'stopped' ? 'default' : 'gold'}>{value || '-'}</Tag> },
    { title: '执行节点', key: 'relay_node_uid', width: 170, render: (_, row) => row.relay_node_name || row.relay_node_uid || '-' },
    { title: '同步时间', key: 'runtime_reconcile_time', width: 170, render: (_, row) => row.runtime_reconcile_time ? getLocalTime(row.runtime_reconcile_time) : '-' },
    { title: '维护时间', key: 'runtime_maintenance_time', width: 170, render: (_, row) => row.runtime_maintenance_time ? getLocalTime(row.runtime_maintenance_time) : '-' },
    { title: '异常持续', key: 'runtime_unhealthy_elapsed_seconds', width: 120, render: (_, row) => row.runtime_unhealthy_elapsed_seconds ? formatDuration(row.runtime_unhealthy_elapsed_seconds) : '-' },
    { title: '账期', dataIndex: 'period', key: 'period', width: 100 },
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '扣后余额', key: 'balance_after_cents', width: 120, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '状态', dataIndex: 'status', key: 'status', width: 100, render: (value) => dataPushJobStatusTag(value) },
    { title: '失败原因', dataIndex: 'failure_reason', key: 'failure_reason', width: 180, render: (value) => value || '-' },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
    {
      title: '操作',
      key: 'actions',
      width: 190,
      render: (_, row) => (
        <Space>
          <Button size="small" onClick={() => reconcileDataPushJob(row)}>同步</Button>
          <Button size="small" onClick={() => controlDataPushJob(row, 'retry')}>重试</Button>
          <Button size="small" danger onClick={() => controlDataPushJob(row, 'cancel')}>取消</Button>
        </Space>
      ),
    },
  ];

  const stationColumns: ColumnsType<StationRecord & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 170 },
    { title: '站点 ID', dataIndex: 'station_id', key: 'station_id', width: 190, render: (value) => value || '-' },
    { title: '在线', key: 'current_online', width: 90, render: (_, row) => <Tag color={row.current_online ? 'green' : 'default'}>{row.current_online ? '在线' : '离线'}</Tag> },
    { title: '累计在线', key: 'total_online_seconds', width: 120, render: (_, row) => formatDuration(row.total_online_seconds ?? 0) },
    { title: '最近在线', key: 'last_seen_time', width: 170, render: (_, row) => getLocalTime(row.last_seen_time ?? 0) },
  ];

  const supplyColumns: ColumnsType<SupplierSupplyUsage & { key: string }> = [
    { title: 'Usage ID', dataIndex: 'usage_id', key: 'usage_id', width: 230 },
    { title: 'AccessAccount', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 150 },
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '收益', key: 'earning_cents', width: 120, render: (_, row) => formatCents(row.earning_cents) },
    { title: '状态', dataIndex: 'status', key: 'status', width: 100, render: (value) => <Tag color={value === 'settled' ? 'green' : 'gold'}>{value || 'pending'}</Tag> },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const settlementColumns: ColumnsType<SupplierSettlementRecord & { key: string }> = [
    { title: 'Settlement ID', dataIndex: 'settlement_id', key: 'settlement_id', width: 260 },
    { title: '账期', dataIndex: 'period', key: 'period', width: 100 },
    { title: '用量数', dataIndex: 'usage_count', key: 'usage_count', width: 90 },
    { title: '供应时长', key: 'total_supply_seconds', width: 120, render: (_, row) => formatDuration(row.total_supply_seconds ?? 0) },
    { title: '结算收益', key: 'total_earning_cents', width: 120, render: (_, row) => formatCents(row.total_earning_cents) },
    { title: '付款状态', dataIndex: 'status', key: 'status', width: 130, render: (value) => settlementStatusTag(value) },
    { title: '付款方式', dataIndex: 'payment_method', key: 'payment_method', width: 120, render: (value) => value || '-' },
    { title: '付款流水', dataIndex: 'payment_ref', key: 'payment_ref', width: 160, render: (value) => value || '-' },
    { title: '付款时间', key: 'paid_time', width: 170, render: (_, row) => row.paid_time ? getLocalTime(row.paid_time) : '-' },
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
    { title: '备注', dataIndex: 'operator_note', key: 'operator_note', ellipsis: true, render: (value) => value || '-' },
  ];

  const dashboard = dashboardQuery.data;
  const earnings = earningsQuery.data;
  const title = scope === 'supplier' ? '供应商工作台' : '用户工作台';
  const accessTitle = scope === 'supplier' ? '基站接入账号' : '用户接入账号';

  const renderProfile = (profile: OperationsAccount | null) => (
    <Descriptions column={{ xs: 1, md: 2 }} bordered size="small">
      <Descriptions.Item label="Account ID">{profile?.account_id || session.account_id || '-'}</Descriptions.Item>
      <Descriptions.Item label="用户名">{profile?.username || session.username}</Descriptions.Item>
      <Descriptions.Item label="角色"><Tag>{profile?.role || session.role}</Tag></Descriptions.Item>
      <Descriptions.Item label="状态">{statusTag(profile?.status || session.status)}</Descriptions.Item>
      <Descriptions.Item label="余额">{formatCents(profile?.balance_cents)}</Descriptions.Item>
      <Descriptions.Item label="并发限制">{profile?.concurrency_limit ?? '-'}</Descriptions.Item>
      <Descriptions.Item label="更新时间">{getLocalTime(profile?.update_time ?? 0)}</Descriptions.Item>
      <Descriptions.Item label="备注">{profile?.remark || '-'}</Descriptions.Item>
    </Descriptions>
  );

  const renderDataPush = () => {
    const totalDebit = dataPushRows.reduce((sum, row) => sum + Number(row.actual_debit_cents ?? 0), 0);
    const totalSeconds = dataPushRows.reduce((sum, row) => sum + Number(row.used_seconds ?? 0), 0);
    return (
      <Row gutter={[16, 16]}>
        <Col xs={12} md={6}><MetricCard title="可用配置" value={dataPushConfigRows.length} prefix={<SwapOutlined />} /></Col>
        <Col xs={12} md={6}><MetricCard title="推送任务" value={dataPushJobRows.length} prefix={<BranchesOutlined />} /></Col>
        <Col xs={12} md={6}><MetricCard title="推送时长" value={formatDuration(totalSeconds)} prefix={<CloudServerOutlined />} /></Col>
        <Col xs={12} md={6}><MetricCard title="推送扣费" value={formatCents(totalDebit)} prefix={<WalletOutlined />} /></Col>
        <Col span={24}>
          <Table columns={dataPushConfigColumns} dataSource={dataPushConfigRows} loading={dataPushConfigQuery.loading} rowKey="key" size="small" scroll={{ x: 1160 }} />
        </Col>
        <Col span={24}>
          <Table columns={dataPushJobColumns} dataSource={dataPushJobRows} loading={dataPushJobQuery.loading} rowKey="key" size="small" scroll={{ x: 2770 }} />
        </Col>
        <Col span={24}>
          <Table columns={dataPushColumns} dataSource={dataPushRows} loading={dataPushQuery.loading} rowKey="key" size="small" scroll={{ x: 1340 }} />
        </Col>
      </Row>
    );
  };

  const renderTable = () => {
    if (view === 'access-accounts') {
      return (
        <Table
          columns={accessColumns}
          dataSource={accessRows}
          loading={accessQuery.loading}
          rowKey="key"
          size="small"
          scroll={{ x: 1280 }}
        />
      );
    }
    if (view === 'profile') return renderProfile(profileQuery.data);
    if (scope === 'me' && view === 'groups') {
      return <Table columns={groupColumns} dataSource={groupRows} loading={groupsQuery.loading} rowKey="key" size="small" scroll={{ x: 860 }} />;
    }
    if (scope === 'me' && view === 'mount-points') {
      return <Table columns={mountColumns} dataSource={mountRows} loading={mountsQuery.loading} rowKey="key" size="small" scroll={{ x: 760 }} />;
    }
    if (scope === 'me' && view === 'usage') {
      return <Table columns={usageColumns} dataSource={usageRows} loading={usageQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
    }
    if (scope === 'me' && view === 'subscriptions') {
      return <Table columns={subscriptionColumns} dataSource={subscriptionRows} loading={subscriptionQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
    }
    if (scope === 'me' && view === 'redeem-redemptions') {
      return <Table columns={redemptionColumns} dataSource={redemptionRows} loading={redemptionQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
    }
    if (scope === 'me' && view === 'data-push') {
      return renderDataPush();
    }
    if (scope === 'supplier' && view === 'stations') {
      return <Table columns={stationColumns} dataSource={stationRows} loading={stationsQuery.loading} rowKey="key" size="small" scroll={{ x: 900 }} />;
    }
    if (scope === 'supplier' && view === 'supply-usage') {
      return <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
    }
    if (scope === 'supplier' && view === 'settlements') {
      return <Table columns={settlementColumns} dataSource={settlementRows} loading={settlementQuery.loading} rowKey="key" size="small" scroll={{ x: 1500 }} />;
    }
    if (scope === 'supplier' && view === 'earnings') {
      return renderEarnings(earnings);
    }
    return null;
  };

  const renderEarnings = (summary: SupplierEarningsSummary | null) => (
    <Row gutter={[16, 16]}>
      <Col xs={12} md={6}><MetricCard title="供应时长" value={formatDuration(summary?.total_supply_seconds ?? 0)} prefix={<CloudServerOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="待结算" value={formatCents(summary?.pending_earning_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="待付款" value={formatCents(summary?.pending_payment_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="已付款" value={formatCents(summary?.paid_earning_cents ?? summary?.settled_earning_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="付款失败" value={formatCents(summary?.failed_payment_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="累计收益" value={formatCents(summary?.total_earning_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="结算批次" value={summary?.settlement_count ?? settlementRows.length} prefix={<BranchesOutlined />} /></Col>
      <Col span={24}>
        <Table columns={settlementColumns} dataSource={settlementRows} loading={settlementQuery.loading} rowKey="key" size="small" scroll={{ x: 1500 }} />
      </Col>
      <Col span={24}>
        <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />
      </Col>
    </Row>
  );

  const extra = view === 'access-accounts'
    ? <Button type="primary" icon={<PlusOutlined />} onClick={openCreate}>{accessTitle}</Button>
    : scope === 'me' && view === 'data-push'
      ? <Button type="primary" icon={<PlusOutlined />} onClick={openDataPushJob}>推送任务</Button>
      : undefined;

  const viewTitle = (() => {
    if (view === 'dashboard') return title;
    if (view === 'profile') return '账号资料';
    if (view === 'access-accounts') return accessTitle;
    if (view === 'groups') return '授权分组';
    if (view === 'mount-points') return '可用挂载点';
    if (view === 'usage') return '计费用量';
    if (view === 'subscriptions') return '订阅';
    if (view === 'redeem-redemptions') return '兑换记录';
    if (view === 'data-push') return '数据推送';
    if (view === 'stations') return '供应站点';
    if (view === 'supply-usage') return '供应时长';
    if (view === 'settlements') return '供应结算';
    return '供应收益';
  })();

  return (
    <PageContainer title={viewTitle} subtitle={`当前账期 ${currentPeriod()}`} extra={extra}>
      {view === 'dashboard' ? (
        <>
          <Row gutter={[16, 16]}>
            <Col xs={12} md={6}>
              <MetricCard title="接入账号" value={(dashboard as RoleDashboard | null)?.access_account_count ?? accessRows.length} prefix={<LockOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title={scope === 'supplier' ? '供应事实' : '授权分组'} value={scope === 'supplier' ? (dashboard?.supply_usage_count ?? supplyRows.length) : (dashboard?.allowed_group_count ?? groupRows.length)} prefix={<BranchesOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title={scope === 'supplier' ? '供应收益' : '余额'} value={scope === 'supplier' ? formatCents(earnings?.total_earning_cents) : formatCents(dashboard?.balance_cents)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="并发限制" value={dashboard?.concurrency_limit ?? profileQuery.data?.concurrency_limit ?? '-'} prefix={<ProfileOutlined />} />
            </Col>
          </Row>
          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} xl={scope === 'supplier' ? 12 : 14}>
              <Table columns={accessColumns.slice(0, 7)} dataSource={accessRows.slice(0, 8)} loading={accessQuery.loading} rowKey="key" size="small" pagination={false} scroll={{ x: 920 }} />
            </Col>
            <Col xs={24} xl={scope === 'supplier' ? 12 : 10}>
              {scope === 'supplier'
                ? <Table columns={stationColumns.slice(0, 5)} dataSource={stationRows.slice(0, 8)} loading={stationsQuery.loading} rowKey="key" size="small" pagination={false} scroll={{ x: 780 }} />
                : <Table columns={groupColumns.slice(0, 4)} dataSource={groupRows.slice(0, 8)} loading={groupsQuery.loading} rowKey="key" size="small" pagination={false} />}
            </Col>
          </Row>
        </>
      ) : renderTable()}

      <Modal title={editing ? '编辑接入账号' : `创建${accessTitle}`} open={modalOpen} onOk={submitAccessAccount} onCancel={() => setModalOpen(false)} width={640}>
        <Form form={form} layout="vertical" size="small">
          {!editing && (
            <Row gutter={16}>
              <Col span={12}>
                <Form.Item name="access_account_id" label="AccessAccount ID" rules={[{ required: true }]}>
                  <Input />
                </Form.Item>
              </Col>
              <Col span={12}>
                <Form.Item name="username" label="接入用户名" rules={[{ required: true }]}>
                  <Input />
                </Form.Item>
              </Col>
              <Col span={24}>
                <Form.Item name="password" label="接入密码" rules={[{ required: true }]}>
                  <Input.Password />
                </Form.Item>
              </Col>
            </Row>
          )}
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="mount_point_group_id" label="挂载点分组" rules={[{ required: true }]}>
                {scope === 'me' ? (
                  <Select
                    showSearch
                    options={groupOptions}
                    placeholder="选择已授权分组"
                    notFoundContent="暂无授权分组"
                  />
                ) : (
                  <Input placeholder="输入供应分组 ID" />
                )}
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="status" label="状态">
                <Select options={[{ value: 'active' }, { value: 'disabled' }]} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="concurrency_limit" label="并发限制">
                <InputNumber min={0} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="expire_time" label="过期时间 UTC 秒(0=长期)">
                <InputNumber min={0} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="private_remark" label="备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="创建数据推送任务" open={dataPushJobOpen} onOk={submitDataPushJob} onCancel={() => setDataPushJobOpen(false)} width={560}>
        <Form form={dataPushJobForm} layout="vertical" size="small">
          <Form.Item name="config_id" label="推送配置" rules={[{ required: true }]}>
            <Select
              showSearch
              options={dataPushConfigOptions}
              placeholder="选择可用推送配置"
              notFoundContent="暂无可用推送配置"
            />
          </Form.Item>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="used_seconds" label="推送时长(秒)" rules={[{ required: true }]}>
                <InputNumber min={1} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="period" label="账期" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="operator_note" label="备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="更新接入密码" open={!!passwordTarget} onOk={submitPassword} onCancel={() => setPasswordTarget(null)} width={420}>
        <Form form={passwordForm} layout="vertical" size="small">
          <Form.Item name="password" label="新密码" rules={[{ required: true }]}>
            <Input.Password />
          </Form.Item>
        </Form>
      </Modal>
    </PageContainer>
  );
};

export default SelfServiceWorkspace;
