import React, { useEffect, useMemo, useState } from 'react';
import {
  Button,
  Col,
  Form,
  Input,
  InputNumber,
  Modal,
  Row,
  Select,
  Space,
  Switch,
  Table,
  Tag,
  message,
} from 'antd';
import {
  BranchesOutlined,
  CheckCircleOutlined,
  CloudServerOutlined,
  DatabaseOutlined,
  LockOutlined,
  PlusOutlined,
  SaveOutlined,
  StopOutlined,
  SyncOutlined,
  SwapOutlined,
  TeamOutlined,
  WalletOutlined,
} from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import PageContainer from '../components/PageContainer';
import MetricCard from '../components/MetricCard';
import { usePolling } from '../hooks/usePolling';
import { adminApi } from '../api/operations';
import type {
  AccessAccountRecord,
  BillingUsageEntry,
  DataPushConfig,
  DataPushJob,
  DataPushUsage,
  HashRecord,
  MountPointGroup,
  MountPointRecord,
  OperationsAccount,
  RedeemCodeRecord,
  StationRecord,
  SubscriptionRecord,
  SupplierSettlementRecord,
  SupplierSupplyUsage,
} from '../api/types';
import { PushType } from '../api/types';
import { currentPeriod, formatCents, formatDuration, getLocalTime } from '../utils/format';

type AdminView =
  | 'dashboard'
  | 'accounts'
  | 'access-accounts'
  | 'mount-point-groups'
  | 'mount-points'
  | 'stations'
  | 'usage'
  | 'subscriptions'
  | 'redeem-codes'
  | 'data-push-configs'
  | 'data-push-jobs'
  | 'data-push-usage'
  | 'supply-usage'
  | 'supplier-settlements';

interface OperationsDashboardProps {
  scope: 'admin';
  view?: AdminView;
}

function rowsFromHash<T extends object>(records: HashRecord<T> | null | undefined): Array<T & { key: string }> {
  return Object.entries(records ?? {}).map(([key, value]) => ({ key, ...value }));
}

function statusTag(status?: string) {
  const color = status === 'active' ? 'green' : status === 'disabled' ? 'orange' : status === 'deleted' ? 'red' : 'default';
  return <Tag color={color}>{status || '-'}</Tag>;
}

function roleTag(role?: string) {
  const color = role === 'admin' ? 'gold' : role === 'supplier' ? 'blue' : role === 'user' ? 'green' : 'default';
  return <Tag color={color}>{role || '-'}</Tag>;
}

function kindTag(kind?: string) {
  return <Tag color={kind === 'supplier_station' ? 'blue' : 'green'}>{kind || '-'}</Tag>;
}

function settlementStatusTag(status?: string) {
  const color = status === 'paid' || status === 'settled'
    ? 'green'
    : status === 'pending_payment'
      ? 'gold'
      : status === 'payment_failed'
        ? 'red'
        : status === 'cancelled' || status === 'void'
          ? 'default'
          : 'blue';
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

function recordCount<T extends object>(records: HashRecord<T> | null): number {
  return Object.keys(records ?? {}).length;
}

const OperationsDashboard: React.FC<OperationsDashboardProps> = ({ view = 'dashboard' }) => {
  const [accountForm] = Form.useForm();
  const [groupForm] = Form.useForm();
  const [memberForm] = Form.useForm();
  const [mountForm] = Form.useForm();
  const [subscriptionForm] = Form.useForm();
  const [redeemForm] = Form.useForm();
  const [redeemApplyForm] = Form.useForm();
  const [dataPushConfigForm] = Form.useForm();
  const [dataPushMaintenanceForm] = Form.useForm();
  const [settlementForm] = Form.useForm();
  const [paymentForm] = Form.useForm();
  const [accountOpen, setAccountOpen] = useState(false);
  const [groupOpen, setGroupOpen] = useState(false);
  const [memberGroupId, setMemberGroupId] = useState<string | null>(null);
  const [mountOpen, setMountOpen] = useState(false);
  const [subscriptionOpen, setSubscriptionOpen] = useState(false);
  const [redeemOpen, setRedeemOpen] = useState(false);
  const [redeemApplyCode, setRedeemApplyCode] = useState<string | null>(null);
  const [dataPushConfigOpen, setDataPushConfigOpen] = useState(false);
  const [settlementOpen, setSettlementOpen] = useState(false);
  const [paymentTarget, setPaymentTarget] = useState<SupplierSettlementRecord | null>(null);
  const [paymentStatus, setPaymentStatus] = useState<'paid' | 'payment_failed' | 'cancelled'>('paid');

  const accountsQuery = usePolling(() => adminApi.accounts(), 5000, view === 'dashboard' || view === 'accounts');
  const accessAccountsQuery = usePolling(() => adminApi.accessAccounts(), 5000, view === 'dashboard' || view === 'access-accounts');
  const groupsQuery = usePolling(() => adminApi.mountPointGroups(), 5000, view === 'dashboard' || view === 'mount-point-groups');
  const mountsQuery = usePolling(() => adminApi.mountPoints(), 5000, view === 'dashboard' || view === 'mount-points');
  const stationsQuery = usePolling(() => adminApi.stations(), 5000, view === 'dashboard' || view === 'stations');
  const usageQuery = usePolling(() => adminApi.usage(currentPeriod()), 5000, view === 'dashboard' || view === 'usage');
  const subscriptionQuery = usePolling(() => adminApi.subscriptions(), 5000, view === 'dashboard' || view === 'subscriptions');
  const redeemQuery = usePolling(() => adminApi.redeemCodes(), 5000, view === 'dashboard' || view === 'redeem-codes');
  const dataPushConfigQuery = usePolling(() => adminApi.dataPushConfigs(), 5000, view === 'dashboard' || view === 'data-push-configs');
  const dataPushJobQuery = usePolling(() => adminApi.dataPushJobs(currentPeriod()), 5000, view === 'dashboard' || view === 'data-push-jobs');
  const dataPushMaintenanceQuery = usePolling(() => adminApi.dataPushMaintenanceConfig(), 5000, view === 'data-push-jobs');
  const dataPushQuery = usePolling(() => adminApi.dataPushUsage(currentPeriod()), 5000, view === 'dashboard' || view === 'data-push-usage');
  const supplyQuery = usePolling(() => adminApi.supplyUsage(currentPeriod()), 5000, view === 'dashboard' || view === 'supply-usage');
  const settlementQuery = usePolling(() => adminApi.supplierSettlements(currentPeriod()), 5000, view === 'dashboard' || view === 'supplier-settlements');

  const accountRows = useMemo(() => rowsFromHash(accountsQuery.data), [accountsQuery.data]);
  const accessRows = useMemo(() => rowsFromHash(accessAccountsQuery.data), [accessAccountsQuery.data]);
  const groupRows = useMemo(() => rowsFromHash(groupsQuery.data), [groupsQuery.data]);
  const mountRows = useMemo(() => rowsFromHash(mountsQuery.data), [mountsQuery.data]);
  const stationRows = useMemo(() => rowsFromHash(stationsQuery.data), [stationsQuery.data]);
  const usageRows = useMemo(() => rowsFromHash(usageQuery.data), [usageQuery.data]);
  const subscriptionRows = useMemo(() => rowsFromHash(subscriptionQuery.data), [subscriptionQuery.data]);
  const redeemRows = useMemo(() => rowsFromHash(redeemQuery.data), [redeemQuery.data]);
  const dataPushConfigRows = useMemo(() => rowsFromHash(dataPushConfigQuery.data), [dataPushConfigQuery.data]);
  const dataPushJobRows = useMemo(() => rowsFromHash(dataPushJobQuery.data), [dataPushJobQuery.data]);
  const dataPushRows = useMemo(() => rowsFromHash(dataPushQuery.data), [dataPushQuery.data]);
  const supplyRows = useMemo(() => rowsFromHash(supplyQuery.data), [supplyQuery.data]);
  const settlementRows = useMemo(() => rowsFromHash(settlementQuery.data), [settlementQuery.data]);

  const totalBalance = accountRows.reduce((sum, account) => sum + Number(account.balance_cents ?? 0), 0);
  const totalSupplySeconds = supplyRows.reduce((sum, usage) => sum + Number(usage.used_seconds ?? 0), 0);
  const totalEarnings = supplyRows.reduce((sum, usage) => sum + Number(usage.earning_cents ?? 0), 0);
  const settledEarnings = settlementRows.reduce((sum, settlement) => sum + Number(settlement.total_earning_cents ?? 0), 0);
  const pendingPaymentEarnings = settlementRows
    .filter((settlement) => !settlement.status || settlement.status === 'pending_payment')
    .reduce((sum, settlement) => sum + Number(settlement.total_earning_cents ?? 0), 0);
  const paidEarnings = settlementRows
    .filter((settlement) => settlement.status === 'paid' || settlement.status === 'settled')
    .reduce((sum, settlement) => sum + Number(settlement.total_earning_cents ?? 0), 0);
  const currentUsageCost = usageRows.reduce((sum, usage) => sum + Number(usage.stat_cost_cents ?? 0), 0);
  const currentDataPushDebit = dataPushRows.reduce((sum, usage) => sum + Number(usage.actual_debit_cents ?? 0), 0);

  useEffect(() => {
    const config = dataPushMaintenanceQuery.data;
    if (!config) return;
    dataPushMaintenanceForm.setFieldsValue({
      enabled: config.enabled ?? true,
      interval_seconds: config.interval_seconds ?? 60,
      unhealthy_after_seconds: config.unhealthy_after_seconds ?? 300,
    });
  }, [
    dataPushMaintenanceForm,
    dataPushMaintenanceQuery.data?.config_id,
    dataPushMaintenanceQuery.data?.enabled,
    dataPushMaintenanceQuery.data?.interval_seconds,
    dataPushMaintenanceQuery.data?.unhealthy_after_seconds,
    dataPushMaintenanceQuery.data?.update_time,
  ]);

  const openCreateAccount = () => {
    accountForm.resetFields();
    accountForm.setFieldsValue({
      account_id: `acc_${Date.now()}`,
      role: 'user',
      status: 'active',
      balance_cents: 0,
      credit_limit_cents: 0,
      concurrency_limit: 1,
    });
    setAccountOpen(true);
  };

  const submitAccount = async () => {
    const values = await accountForm.validateFields();
    await adminApi.createAccount(values);
    message.success('账号已创建');
    setAccountOpen(false);
    accountsQuery.refresh();
  };

  const openCreateGroup = () => {
    groupForm.resetFields();
    groupForm.setFieldsValue({ group_id: `mpg_${Date.now()}`, billing_multiplier: 1, status: 'active' });
    setGroupOpen(true);
  };

  const submitGroup = async () => {
    const values = await groupForm.validateFields();
    await adminApi.createMountPointGroup(values);
    message.success('挂载点分组已创建');
    setGroupOpen(false);
    groupsQuery.refresh();
  };

  const openMember = (groupId: string) => {
    memberForm.resetFields();
    setMemberGroupId(groupId);
  };

  const submitMember = async () => {
    if (!memberGroupId) return;
    const values = await memberForm.validateFields();
    await adminApi.addGroupMember(memberGroupId, values.mountpoint);
    message.success('分组成员已更新');
    setMemberGroupId(null);
  };

  const openMountPoint = () => {
    mountForm.resetFields();
    mountForm.setFieldsValue({ status: 'active', hourly_price_cents: 0 });
    setMountOpen(true);
  };

  const submitMountPoint = async () => {
    const values = await mountForm.validateFields();
    const { mountpoint, ...body } = values;
    await adminApi.upsertMountPoint(mountpoint, body);
    message.success('挂载点已写入');
    setMountOpen(false);
    mountsQuery.refresh();
  };

  const openSubscription = () => {
    subscriptionForm.resetFields();
    subscriptionForm.setFieldsValue({
      subscription_id: `sub_${Date.now()}`,
      status: 'active',
      start_time: 0,
      expire_time: 0,
      group_ids_text: '',
    });
    setSubscriptionOpen(true);
  };

  const submitSubscription = async () => {
    const values = await subscriptionForm.validateFields();
    await adminApi.createSubscription({
      subscription_id: values.subscription_id,
      account_id: values.account_id,
      group_ids: String(values.group_ids_text || '').split(',').map((item) => item.trim()).filter(Boolean),
      status: values.status,
      start_time: values.start_time,
      expire_time: values.expire_time,
      remark: values.remark,
    });
    message.success('订阅已创建');
    setSubscriptionOpen(false);
    subscriptionQuery.refresh();
  };

  const disableSubscription = async (record: SubscriptionRecord) => {
    await adminApi.updateSubscription(record.subscription_id, { status: 'disabled' });
    message.success('订阅已禁用');
    subscriptionQuery.refresh();
  };

  const openRedeemCode = () => {
    redeemForm.resetFields();
    redeemForm.setFieldsValue({ code: `RC${Date.now()}`, amount_cents: 1000, status: 'active', max_redemptions: 1, expire_time: 0 });
    setRedeemOpen(true);
  };

  const submitRedeemCode = async () => {
    const values = await redeemForm.validateFields();
    await adminApi.createRedeemCode(values);
    message.success('兑换码已创建');
    setRedeemOpen(false);
    redeemQuery.refresh();
  };

  const openRedeemApply = (code: string) => {
    redeemApplyForm.resetFields();
    redeemApplyForm.setFieldsValue({ period: currentPeriod() });
    setRedeemApplyCode(code);
  };

  const submitRedeemApply = async () => {
    if (!redeemApplyCode) return;
    const values = await redeemApplyForm.validateFields();
    await adminApi.redeemCode(redeemApplyCode, values);
    message.success('兑换码已核销');
    setRedeemApplyCode(null);
    redeemQuery.refresh();
    accountsQuery.refresh();
  };

  const openDataPushConfig = () => {
    dataPushConfigForm.resetFields();
    dataPushConfigForm.setFieldsValue({
      config_id: `dpc_${Date.now()}`,
      status: 'active',
      execution_mode: 'ledger_only',
      fixed_hourly_price_cents: 0,
      relay_target_port: 2101,
      relay_push_type: PushType.PUSH_TYPE_NTRIP_1_0,
    });
    setDataPushConfigOpen(true);
  };

  const submitDataPushConfig = async () => {
    const values = await dataPushConfigForm.validateFields();
    await adminApi.createDataPushConfig(values);
    message.success('推送配置已创建');
    setDataPushConfigOpen(false);
    dataPushConfigQuery.refresh();
  };

  const deleteDataPushConfig = async (record: DataPushConfig) => {
    await adminApi.deleteDataPushConfig(record.config_id);
    message.success('推送配置已删除');
    dataPushConfigQuery.refresh();
  };

  const controlDataPushJob = async (record: DataPushJob, action: 'cancel' | 'retry' | 'mark_failed' | 'mark_completed') => {
    await adminApi.controlDataPushJob(record.job_id, {
      action,
      period: record.period || currentPeriod(),
      operator_note: `admin ${action}`,
    });
    message.success('推送任务状态已更新');
    dataPushJobQuery.refresh();
  };

  const reconcileDataPushJob = async (record: DataPushJob) => {
    await adminApi.reconcileDataPushJob(record.job_id, {
      period: record.period || currentPeriod(),
      operator_note: 'admin reconcile',
    });
    message.success('推送任务运行态已同步');
    dataPushJobQuery.refresh();
  };

  const submitDataPushMaintenance = async () => {
    const values = await dataPushMaintenanceForm.validateFields();
    await adminApi.updateDataPushMaintenanceConfig(values);
    message.success('维护配置已保存');
    dataPushMaintenanceQuery.refresh();
  };

  const runDataPushMaintenance = async () => {
    const values = dataPushMaintenanceForm.getFieldsValue();
    const result = await adminApi.runDataPushMaintenance({
      period: currentPeriod(),
      unhealthy_after_seconds: Number(values.unhealthy_after_seconds ?? dataPushMaintenanceQuery.data?.unhealthy_after_seconds ?? 300),
    });
    message.success(`维护完成：更新 ${result.updated_count}，失败 ${result.failed_count}`);
    dataPushJobQuery.refresh();
    dataPushMaintenanceQuery.refresh();
  };

  const openSupplierSettlement = () => {
    settlementForm.resetFields();
    settlementForm.setFieldsValue({ period: currentPeriod() });
    setSettlementOpen(true);
  };

  const submitSupplierSettlement = async () => {
    const values = await settlementForm.validateFields();
    await adminApi.createSupplierSettlement(values);
    message.success('供应商结算已生成');
    setSettlementOpen(false);
    settlementQuery.refresh();
    supplyQuery.refresh();
  };

  const openSettlementPayment = (record: SupplierSettlementRecord, status: 'paid' | 'payment_failed' | 'cancelled') => {
    setPaymentTarget(record);
    setPaymentStatus(status);
    paymentForm.resetFields();
    paymentForm.setFieldsValue({
      supplier_account_id: record.supplier_account_id,
      period: record.period || currentPeriod(),
      payment_method: record.payment_method || 'manual',
      payment_ref: record.payment_ref,
      payment_note: record.payment_note,
    });
  };

  const submitSettlementPayment = async () => {
    if (!paymentTarget) return;
    const values = await paymentForm.validateFields();
    await adminApi.updateSupplierSettlementPayment(paymentTarget.settlement_id, {
      ...values,
      supplier_account_id: paymentTarget.supplier_account_id,
      period: paymentTarget.period || currentPeriod(),
      status: paymentStatus,
    });
    message.success(paymentStatus === 'paid' ? '结算已标记付款' : paymentStatus === 'payment_failed' ? '结算已标记失败' : '结算已取消');
    setPaymentTarget(null);
    settlementQuery.refresh();
  };

  const accountColumns: ColumnsType<OperationsAccount & { key: string }> = [
    { title: 'Account ID', dataIndex: 'account_id', key: 'account_id', width: 190 },
    { title: '用户名', dataIndex: 'username', key: 'username', width: 140 },
    { title: '角色', key: 'role', width: 100, render: (_, row) => roleTag(row.role) },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '余额', key: 'balance_cents', width: 120, render: (_, row) => formatCents(row.balance_cents) },
    { title: '并发', dataIndex: 'concurrency_limit', key: 'concurrency_limit', width: 90 },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
    { title: '备注', dataIndex: 'remark', key: 'remark', ellipsis: true, render: (value) => value || '-' },
  ];

  const accessColumns: ColumnsType<AccessAccountRecord & { key: string }> = [
    { title: 'AccessAccount ID', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '接入用户名', dataIndex: 'username', key: 'username', width: 140 },
    { title: 'Owner', dataIndex: 'owner_account_id', key: 'owner_account_id', width: 190 },
    { title: '类型', key: 'kind', width: 140, render: (_, row) => kindTag(row.kind) },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '分组', dataIndex: 'mount_point_group_id', key: 'mount_point_group_id', width: 160 },
    { title: '并发', dataIndex: 'concurrency_limit', key: 'concurrency_limit', width: 80 },
    { title: '过期时间', key: 'expire_time', width: 170, render: (_, row) => row.expire_time ? getLocalTime(row.expire_time) : '长期' },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
  ];

  const groupColumns: ColumnsType<MountPointGroup & { key: string }> = [
    { title: 'Group ID', dataIndex: 'group_id', key: 'group_id', width: 190 },
    { title: '名称', dataIndex: 'name', key: 'name', width: 160 },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '计费倍率', dataIndex: 'billing_multiplier', key: 'billing_multiplier', width: 110 },
    { title: '描述', dataIndex: 'description', key: 'description', ellipsis: true, render: (value) => value || '-' },
    {
      title: '操作',
      key: 'actions',
      width: 120,
      render: (_, row) => <Button size="small" onClick={() => openMember(row.group_id)}>成员</Button>,
    },
  ];

  const mountColumns: ColumnsType<MountPointRecord & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 180 },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '小时价格', key: 'hourly_price_cents', width: 120, render: (_, row) => formatCents(row.hourly_price_cents) },
    { title: '源记录', dataIndex: 'source_record_mount', key: 'source_record_mount', render: (value) => value || '-' },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
  ];

  const stationColumns: ColumnsType<StationRecord & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 170 },
    { title: '站点 ID', dataIndex: 'station_id', key: 'station_id', width: 190, render: (value) => value || '-' },
    { title: '在线', key: 'current_online', width: 90, render: (_, row) => <Tag color={row.current_online ? 'green' : 'default'}>{row.current_online ? '在线' : '离线'}</Tag> },
    { title: '最近供应商', dataIndex: 'last_supplier_account_id', key: 'last_supplier_account_id', width: 190, render: (value) => value || '-' },
    { title: '累计在线', key: 'total_online_seconds', width: 120, render: (_, row) => formatDuration(row.total_online_seconds ?? 0) },
    { title: '最近在线', key: 'last_seen_time', width: 170, render: (_, row) => getLocalTime(row.last_seen_time ?? 0) },
  ];

  const usageColumns: ColumnsType<BillingUsageEntry & { key: string }> = [
    { title: 'Billing ID', dataIndex: 'billing_id', key: 'billing_id', width: 230 },
    { title: 'Account', dataIndex: 'account_id', key: 'account_id', width: 190 },
    { title: 'AccessAccount', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 150 },
    { title: '模式', dataIndex: 'billing_mode', key: 'billing_mode', width: 110, render: (value) => value || 'payg' },
    { title: '订阅', dataIndex: 'subscription_id', key: 'subscription_id', width: 170, render: (value) => value || '-' },
    { title: '时长', key: 'used_seconds', width: 100, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '实际扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const subscriptionColumns: ColumnsType<SubscriptionRecord & { key: string }> = [
    { title: 'Subscription ID', dataIndex: 'subscription_id', key: 'subscription_id', width: 210 },
    { title: 'Account', dataIndex: 'account_id', key: 'account_id', width: 190 },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '分组', key: 'group_ids', width: 220, render: (_, row) => (row.group_ids || []).join(', ') || '-' },
    { title: '开始', key: 'start_time', width: 170, render: (_, row) => row.start_time ? getLocalTime(row.start_time) : '立即' },
    { title: '过期', key: 'expire_time', width: 170, render: (_, row) => row.expire_time ? getLocalTime(row.expire_time) : '长期' },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
    { title: '操作', key: 'actions', width: 100, render: (_, row) => <Button size="small" danger onClick={() => disableSubscription(row)}>禁用</Button> },
  ];

  const redeemColumns: ColumnsType<RedeemCodeRecord & { key: string }> = [
    { title: 'Code', dataIndex: 'code', key: 'code', width: 180 },
    { title: '金额', key: 'amount_cents', width: 120, render: (_, row) => formatCents(row.amount_cents) },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '次数', key: 'count', width: 100, render: (_, row) => `${row.redeemed_count ?? 0}/${row.max_redemptions ?? 1}` },
    { title: '过期', key: 'expire_time', width: 170, render: (_, row) => row.expire_time ? getLocalTime(row.expire_time) : '长期' },
    { title: '最近核销', dataIndex: 'last_redeemed_account_id', key: 'last_redeemed_account_id', width: 190, render: (value) => value || '-' },
    { title: '操作', key: 'actions', width: 100, render: (_, row) => <Button size="small" onClick={() => openRedeemApply(row.code)}>核销</Button> },
  ];

  const dataPushColumns: ColumnsType<DataPushUsage & { key: string }> = [
    { title: 'Usage ID', dataIndex: 'usage_id', key: 'usage_id', width: 230 },
    { title: 'Account', dataIndex: 'account_id', key: 'account_id', width: 190 },
    { title: '配置', dataIndex: 'config_id', key: 'config_id', width: 160, render: (value) => value || '-' },
    { title: '任务', dataIndex: 'job_id', key: 'job_id', width: 220, render: (value) => value || '-' },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: '时长', key: 'used_seconds', width: 100, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '实际扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '扣后余额', key: 'balance_after_cents', width: 120, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const dataPushConfigColumns: ColumnsType<DataPushConfig & { key: string }> = [
    { title: 'Config ID', dataIndex: 'config_id', key: 'config_id', width: 190 },
    { title: '名称', dataIndex: 'name', key: 'name', width: 180 },
    { title: '执行模式', key: 'execution_mode', width: 130, render: (_, row) => dataPushExecutionTag(row.execution_mode) },
    { title: '源挂载点', dataIndex: 'source_mountpoint', key: 'source_mountpoint', width: 150, render: (value) => value || '-' },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 170 },
    { title: 'Relay 目标', key: 'relay_target_host', width: 190, render: (_, row) => row.execution_mode === 'relay_push' ? `${row.relay_target_host || '-'}:${row.relay_target_port || 2101}` : '-' },
    { title: '分组', dataIndex: 'group_id', key: 'group_id', width: 160, render: (value) => value || '-' },
    { title: '小时价格', key: 'fixed_hourly_price_cents', width: 120, render: (_, row) => formatCents(row.fixed_hourly_price_cents) },
    { title: '状态', key: 'status', width: 100, render: (_, row) => statusTag(row.status) },
    { title: '更新时间', key: 'update_time', width: 170, render: (_, row) => getLocalTime(row.update_time ?? 0) },
    { title: '描述', dataIndex: 'description', key: 'description', ellipsis: true, render: (value) => value || '-' },
    {
      title: '操作',
      key: 'actions',
      width: 100,
      render: (_, row) => <Button size="small" danger onClick={() => deleteDataPushConfig(row)}>删除</Button>,
    },
  ];

  const dataPushJobColumns: ColumnsType<DataPushJob & { key: string }> = [
    { title: 'Job ID', dataIndex: 'job_id', key: 'job_id', width: 260 },
    { title: 'Account', dataIndex: 'account_id', key: 'account_id', width: 190 },
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
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
    {
      title: '操作',
      key: 'actions',
      width: 330,
      render: (_, row) => (
        <Space>
          <Button size="small" onClick={() => reconcileDataPushJob(row)}>同步</Button>
          <Button size="small" onClick={() => controlDataPushJob(row, 'mark_completed')}>完成</Button>
          <Button size="small" onClick={() => controlDataPushJob(row, 'mark_failed')}>失败</Button>
          <Button size="small" onClick={() => controlDataPushJob(row, 'retry')}>重试</Button>
          <Button size="small" danger onClick={() => controlDataPushJob(row, 'cancel')}>取消</Button>
        </Space>
      ),
    },
  ];

  const dataPushMaintenanceControls = (
    <Form
      form={dataPushMaintenanceForm}
      layout="inline"
      size="small"
      initialValues={{ enabled: true, interval_seconds: 60, unhealthy_after_seconds: 300 }}
      style={{ rowGap: 8 }}
    >
      <Form.Item name="enabled" label="自动维护" valuePropName="checked">
        <Switch checkedChildren="开" unCheckedChildren="关" />
      </Form.Item>
      <Form.Item name="interval_seconds" label="周期(秒)" rules={[{ required: true }]}>
        <InputNumber min={5} max={86400} style={{ width: 110 }} />
      </Form.Item>
      <Form.Item name="unhealthy_after_seconds" label="失败阈值(秒)" rules={[{ required: true }]}>
        <InputNumber min={0} max={86400} style={{ width: 120 }} />
      </Form.Item>
      <Form.Item>
        <Button icon={<SaveOutlined />} onClick={submitDataPushMaintenance}>保存</Button>
      </Form.Item>
      <Form.Item>
        <Button icon={<SyncOutlined />} onClick={runDataPushMaintenance}>立即维护</Button>
      </Form.Item>
      <Form.Item>
        <Tag color={dataPushMaintenanceQuery.data?.enabled === false ? 'default' : 'green'}>
          {dataPushMaintenanceQuery.data?.enabled === false ? '已停用' : '运行中'}
        </Tag>
      </Form.Item>
      <Form.Item>
        <span style={{ color: '#666' }}>
          {dataPushMaintenanceQuery.data?.update_time ? getLocalTime(dataPushMaintenanceQuery.data.update_time) : '-'}
        </span>
      </Form.Item>
    </Form>
  );

  const supplyColumns: ColumnsType<SupplierSupplyUsage & { key: string }> = [
    { title: 'Usage ID', dataIndex: 'usage_id', key: 'usage_id', width: 230 },
    { title: '供应商', dataIndex: 'supplier_account_id', key: 'supplier_account_id', width: 190 },
    { title: 'AccessAccount', dataIndex: 'access_account_id', key: 'access_account_id', width: 210 },
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 150 },
    { title: '时长', key: 'used_seconds', width: 100, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '收益', key: 'earning_cents', width: 120, render: (_, row) => formatCents(row.earning_cents) },
    { title: '状态', dataIndex: 'status', key: 'status', width: 100, render: (value) => <Tag color={value === 'settled' ? 'green' : 'gold'}>{value || 'pending'}</Tag> },
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const settlementColumns: ColumnsType<SupplierSettlementRecord & { key: string }> = [
    { title: 'Settlement ID', dataIndex: 'settlement_id', key: 'settlement_id', width: 260 },
    { title: '供应商', dataIndex: 'supplier_account_id', key: 'supplier_account_id', width: 190 },
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
    {
      title: '操作',
      key: 'action',
      width: 180,
      fixed: 'right',
      render: (_, row) => (
        <Space size="small">
          <Button size="small" icon={<CheckCircleOutlined />} disabled={row.status === 'paid' || row.status === 'settled'} onClick={() => openSettlementPayment(row, 'paid')}>付款</Button>
          <Button size="small" danger icon={<StopOutlined />} disabled={row.status === 'paid' || row.status === 'settled'} onClick={() => openSettlementPayment(row, 'payment_failed')}>失败</Button>
        </Space>
      ),
    },
  ];

  const table = (() => {
    if (view === 'accounts') {
      return (
        <Table
          columns={accountColumns}
          dataSource={accountRows}
          loading={accountsQuery.loading}
          rowKey="key"
          size="small"
          scroll={{ x: 1100 }}
        />
      );
    }
    if (view === 'access-accounts') {
      return <Table columns={accessColumns} dataSource={accessRows} loading={accessAccountsQuery.loading} rowKey="key" size="small" scroll={{ x: 1260 }} />;
    }
    if (view === 'mount-point-groups') {
      return <Table columns={groupColumns} dataSource={groupRows} loading={groupsQuery.loading} rowKey="key" size="small" scroll={{ x: 900 }} />;
    }
    if (view === 'mount-points') {
      return <Table columns={mountColumns} dataSource={mountRows} loading={mountsQuery.loading} rowKey="key" size="small" scroll={{ x: 900 }} />;
    }
    if (view === 'stations') {
      return <Table columns={stationColumns} dataSource={stationRows} loading={stationsQuery.loading} rowKey="key" size="small" scroll={{ x: 1000 }} />;
    }
    if (view === 'usage') {
      return <Table columns={usageColumns} dataSource={usageRows} loading={usageQuery.loading} rowKey="key" size="small" scroll={{ x: 1300 }} />;
    }
    if (view === 'subscriptions') {
      return <Table columns={subscriptionColumns} dataSource={subscriptionRows} loading={subscriptionQuery.loading} rowKey="key" size="small" scroll={{ x: 1400 }} />;
    }
    if (view === 'redeem-codes') {
      return <Table columns={redeemColumns} dataSource={redeemRows} loading={redeemQuery.loading} rowKey="key" size="small" scroll={{ x: 1000 }} />;
    }
    if (view === 'data-push-configs') {
      return <Table columns={dataPushConfigColumns} dataSource={dataPushConfigRows} loading={dataPushConfigQuery.loading} rowKey="key" size="small" scroll={{ x: 1600 }} />;
    }
    if (view === 'data-push-jobs') {
      return (
        <Space direction="vertical" size={12} style={{ width: '100%' }}>
          {dataPushMaintenanceControls}
          <Table columns={dataPushJobColumns} dataSource={dataPushJobRows} loading={dataPushJobQuery.loading} rowKey="key" size="small" scroll={{ x: 2990 }} />
        </Space>
      );
    }
    if (view === 'data-push-usage') {
      return <Table columns={dataPushColumns} dataSource={dataPushRows} loading={dataPushQuery.loading} rowKey="key" size="small" scroll={{ x: 1600 }} />;
    }
    if (view === 'supply-usage') {
      return <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1300 }} />;
    }
    if (view === 'supplier-settlements') {
      return <Table columns={settlementColumns} dataSource={settlementRows} loading={settlementQuery.loading} rowKey="key" size="small" scroll={{ x: 1800 }} />;
    }
    return null;
  })();

  const titleMap: Record<AdminView, string> = {
    dashboard: '运营总览',
    accounts: '账号',
    'access-accounts': '接入账号',
    'mount-point-groups': '挂载点分组',
    'mount-points': '挂载点记录',
    stations: '历史站点',
    usage: '计费用量',
    subscriptions: '订阅',
    'redeem-codes': '兑换码',
    'data-push-configs': '数据推送配置',
    'data-push-jobs': '数据推送任务',
    'data-push-usage': '数据推送用量',
    'supply-usage': '供应事实',
    'supplier-settlements': '供应商结算',
  };

  const extra = (
    <Space>
      {view === 'accounts' && <Button type="primary" icon={<PlusOutlined />} onClick={openCreateAccount}>账号</Button>}
      {view === 'mount-point-groups' && <Button type="primary" icon={<PlusOutlined />} onClick={openCreateGroup}>分组</Button>}
      {view === 'mount-points' && <Button type="primary" icon={<PlusOutlined />} onClick={openMountPoint}>挂载点</Button>}
      {view === 'subscriptions' && <Button type="primary" icon={<PlusOutlined />} onClick={openSubscription}>订阅</Button>}
      {view === 'redeem-codes' && <Button type="primary" icon={<PlusOutlined />} onClick={openRedeemCode}>兑换码</Button>}
      {view === 'data-push-configs' && <Button type="primary" icon={<PlusOutlined />} onClick={openDataPushConfig}>推送配置</Button>}
      {view === 'supplier-settlements' && <Button type="primary" icon={<PlusOutlined />} onClick={openSupplierSettlement}>结算</Button>}
    </Space>
  );

  return (
    <PageContainer title={titleMap[view]} subtitle={`当前账期 ${currentPeriod()}`} extra={extra}>
      {view === 'dashboard' ? (
        <>
          <Row gutter={[16, 16]}>
            <Col xs={12} md={6}>
              <MetricCard title="账号" value={recordCount(accountsQuery.data)} prefix={<TeamOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="接入账号" value={recordCount(accessAccountsQuery.data)} prefix={<LockOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="挂载点" value={recordCount(mountsQuery.data)} prefix={<DatabaseOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="历史站点" value={recordCount(stationsQuery.data)} prefix={<CloudServerOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="账户余额" value={formatCents(totalBalance)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="本期计费" value={formatCents(currentUsageCost)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="数据推送扣费" value={formatCents(currentDataPushDebit)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="推送配置" value={recordCount(dataPushConfigQuery.data)} prefix={<SwapOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="推送任务" value={recordCount(dataPushJobQuery.data)} prefix={<SwapOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="供应时长" value={formatDuration(totalSupplySeconds)} prefix={<BranchesOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="供应收益" value={formatCents(totalEarnings)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="已结算收益" value={formatCents(settledEarnings)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="待付款收益" value={formatCents(pendingPaymentEarnings)} prefix={<WalletOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="已付款收益" value={formatCents(paidEarnings)} prefix={<WalletOutlined />} />
            </Col>
          </Row>
          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} xl={12}>
              <Table columns={accountColumns.slice(0, 6)} dataSource={accountRows.slice(0, 8)} loading={accountsQuery.loading} rowKey="key" size="small" pagination={false} />
            </Col>
            <Col xs={24} xl={12}>
              <Table columns={supplyColumns.slice(0, 6)} dataSource={supplyRows.slice(0, 8)} loading={supplyQuery.loading} rowKey="key" size="small" pagination={false} scroll={{ x: 900 }} />
            </Col>
          </Row>
        </>
      ) : table}

      <Modal title="创建账号" open={accountOpen} onOk={submitAccount} onCancel={() => setAccountOpen(false)} width={640}>
        <Form form={accountForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="account_id" label="Account ID" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="username" label="用户名" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="password" label="密码" rules={[{ required: true }]}>
                <Input.Password />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="role" label="角色" rules={[{ required: true }]}>
                <Select options={[
                  { value: 'user', label: 'user' },
                  { value: 'supplier', label: 'supplier' },
                  { value: 'admin', label: 'admin' },
                ]} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="status" label="状态">
                <Select options={[{ value: 'active' }, { value: 'disabled' }]} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="balance_cents" label="余额(分)">
                <InputNumber min={0} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="concurrency_limit" label="并发">
                <InputNumber min={0} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="remark" label="备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="创建挂载点分组" open={groupOpen} onOk={submitGroup} onCancel={() => setGroupOpen(false)} width={560}>
        <Form form={groupForm} layout="vertical" size="small">
          <Form.Item name="group_id" label="Group ID" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Form.Item name="name" label="名称" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="status" label="状态">
                <Select options={[{ value: 'active' }, { value: 'disabled' }]} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="billing_multiplier" label="计费倍率">
                <InputNumber min={0} step={0.1} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="description" label="描述">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="写入分组成员" open={!!memberGroupId} onOk={submitMember} onCancel={() => setMemberGroupId(null)} width={460}>
        <Form form={memberForm} layout="vertical" size="small">
          <Form.Item name="mountpoint" label="挂载点名" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="创建或更新挂载点" open={mountOpen} onOk={submitMountPoint} onCancel={() => setMountOpen(false)} width={560}>
        <Form form={mountForm} layout="vertical" size="small">
          <Form.Item name="mountpoint" label="挂载点名" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="status" label="状态">
                <Select options={[{ value: 'active' }, { value: 'disabled' }]} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="hourly_price_cents" label="小时价格(分)">
                <InputNumber min={0} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="source_record_mount" label="源记录挂载点">
            <Input />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="创建订阅" open={subscriptionOpen} onOk={submitSubscription} onCancel={() => setSubscriptionOpen(false)} width={640}>
        <Form form={subscriptionForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}><Form.Item name="subscription_id" label="Subscription ID" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="account_id" label="Account ID" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={24}><Form.Item name="group_ids_text" label="覆盖分组，逗号分隔" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={8}><Form.Item name="status" label="状态"><Select options={[{ value: 'active' }, { value: 'disabled' }]} /></Form.Item></Col>
            <Col span={8}><Form.Item name="start_time" label="开始 UTC 秒"><InputNumber min={0} style={{ width: '100%' }} /></Form.Item></Col>
            <Col span={8}><Form.Item name="expire_time" label="过期 UTC 秒"><InputNumber min={0} style={{ width: '100%' }} /></Form.Item></Col>
          </Row>
          <Form.Item name="remark" label="备注"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>

      <Modal title="创建兑换码" open={redeemOpen} onOk={submitRedeemCode} onCancel={() => setRedeemOpen(false)} width={560}>
        <Form form={redeemForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}><Form.Item name="code" label="Code" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="amount_cents" label="金额(分)" rules={[{ required: true }]}><InputNumber min={1} style={{ width: '100%' }} /></Form.Item></Col>
            <Col span={8}><Form.Item name="status" label="状态"><Select options={[{ value: 'active' }, { value: 'disabled' }]} /></Form.Item></Col>
            <Col span={8}><Form.Item name="max_redemptions" label="可核销次数"><InputNumber min={1} style={{ width: '100%' }} /></Form.Item></Col>
            <Col span={8}><Form.Item name="expire_time" label="过期 UTC 秒"><InputNumber min={0} style={{ width: '100%' }} /></Form.Item></Col>
          </Row>
          <Form.Item name="batch_id" label="批次"><Input /></Form.Item>
          <Form.Item name="note" label="备注"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>

      <Modal title="核销兑换码" open={!!redeemApplyCode} onOk={submitRedeemApply} onCancel={() => setRedeemApplyCode(null)} width={480}>
        <Form form={redeemApplyForm} layout="vertical" size="small">
          <Form.Item name="account_id" label="Account ID" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="period" label="账期"><Input /></Form.Item>
          <Form.Item name="operator_note" label="备注"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>

      <Modal title="创建数据推送配置" open={dataPushConfigOpen} onOk={submitDataPushConfig} onCancel={() => setDataPushConfigOpen(false)} width={760}>
        <Form form={dataPushConfigForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}><Form.Item name="config_id" label="Config ID" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="name" label="名称" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="execution_mode" label="执行模式" rules={[{ required: true }]}><Select options={[{ value: 'ledger_only', label: 'ledger_only' }, { value: 'relay_push', label: 'relay_push' }]} /></Form.Item></Col>
            <Col span={12}><Form.Item name="source_mountpoint" label="源挂载点"><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="target_mountpoint" label="目标挂载点" rules={[{ required: true }]}><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="group_id" label="分组"><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="status" label="状态"><Select options={[{ value: 'active' }, { value: 'disabled' }]} /></Form.Item></Col>
            <Col span={12}><Form.Item name="fixed_hourly_price_cents" label="小时价格(分)" rules={[{ required: true }]}><InputNumber min={0} style={{ width: '100%' }} /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_target_host" label="Relay 目标主机"><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_target_port" label="Relay 目标端口"><InputNumber min={1} style={{ width: '100%' }} /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_target_mountpoint" label="Relay 目标挂载点"><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_push_type" label="Relay 协议"><Select options={[{ value: PushType.PUSH_TYPE_NTRIP_1_0, label: 'NTRIP 1.0' }, { value: PushType.PUSH_TYPE_NTRIP_2_0, label: 'NTRIP 2.0' }]} /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_target_account" label="Relay 账号"><Input /></Form.Item></Col>
            <Col span={12}><Form.Item name="relay_target_password" label="Relay 密码"><Input.Password /></Form.Item></Col>
          </Row>
          <Form.Item name="description" label="描述"><Input.TextArea rows={2} /></Form.Item>
        </Form>
      </Modal>

      <Modal title="创建供应商结算" open={settlementOpen} onOk={submitSupplierSettlement} onCancel={() => setSettlementOpen(false)} width={560}>
        <Form form={settlementForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="supplier_account_id" label="Supplier Account ID" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="period" label="账期" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="external_ref" label="外部参考号">
            <Input />
          </Form.Item>
          <Form.Item name="operator_note" label="备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="更新付款状态" open={!!paymentTarget} onOk={submitSettlementPayment} onCancel={() => setPaymentTarget(null)} width={560}>
        <Form form={paymentForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="supplier_account_id" label="Supplier Account ID" rules={[{ required: true }]}>
                <Input disabled />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="period" label="账期" rules={[{ required: true }]}>
                <Input disabled />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item label="目标状态">
                <Input value={paymentStatus} disabled />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="payment_method" label="付款方式">
                <Input />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="payment_ref" label="付款流水 / 外部单号">
            <Input />
          </Form.Item>
          <Form.Item name="payment_note" label="付款备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>
    </PageContainer>
  );
};

export default OperationsDashboard;
