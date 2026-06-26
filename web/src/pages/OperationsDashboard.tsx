import React, { useMemo, useState } from 'react';
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
  Table,
  Tag,
  message,
} from 'antd';
import {
  BranchesOutlined,
  CloudServerOutlined,
  DatabaseOutlined,
  LockOutlined,
  PlusOutlined,
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
  DataPushUsage,
  HashRecord,
  MountPointGroup,
  MountPointRecord,
  OperationsAccount,
  RedeemCodeRecord,
  StationRecord,
  SubscriptionRecord,
  SupplierSupplyUsage,
} from '../api/types';
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
  | 'data-push-usage'
  | 'supply-usage';

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
  const [accountOpen, setAccountOpen] = useState(false);
  const [groupOpen, setGroupOpen] = useState(false);
  const [memberGroupId, setMemberGroupId] = useState<string | null>(null);
  const [mountOpen, setMountOpen] = useState(false);
  const [subscriptionOpen, setSubscriptionOpen] = useState(false);
  const [redeemOpen, setRedeemOpen] = useState(false);
  const [redeemApplyCode, setRedeemApplyCode] = useState<string | null>(null);

  const accountsQuery = usePolling(() => adminApi.accounts(), 5000, view === 'dashboard' || view === 'accounts');
  const accessAccountsQuery = usePolling(() => adminApi.accessAccounts(), 5000, view === 'dashboard' || view === 'access-accounts');
  const groupsQuery = usePolling(() => adminApi.mountPointGroups(), 5000, view === 'dashboard' || view === 'mount-point-groups');
  const mountsQuery = usePolling(() => adminApi.mountPoints(), 5000, view === 'dashboard' || view === 'mount-points');
  const stationsQuery = usePolling(() => adminApi.stations(), 5000, view === 'dashboard' || view === 'stations');
  const usageQuery = usePolling(() => adminApi.usage(currentPeriod()), 5000, view === 'dashboard' || view === 'usage');
  const subscriptionQuery = usePolling(() => adminApi.subscriptions(), 5000, view === 'dashboard' || view === 'subscriptions');
  const redeemQuery = usePolling(() => adminApi.redeemCodes(), 5000, view === 'dashboard' || view === 'redeem-codes');
  const dataPushQuery = usePolling(() => adminApi.dataPushUsage(currentPeriod()), 5000, view === 'dashboard' || view === 'data-push-usage');
  const supplyQuery = usePolling(() => adminApi.supplyUsage(currentPeriod()), 5000, view === 'dashboard' || view === 'supply-usage');

  const accountRows = useMemo(() => rowsFromHash(accountsQuery.data), [accountsQuery.data]);
  const accessRows = useMemo(() => rowsFromHash(accessAccountsQuery.data), [accessAccountsQuery.data]);
  const groupRows = useMemo(() => rowsFromHash(groupsQuery.data), [groupsQuery.data]);
  const mountRows = useMemo(() => rowsFromHash(mountsQuery.data), [mountsQuery.data]);
  const stationRows = useMemo(() => rowsFromHash(stationsQuery.data), [stationsQuery.data]);
  const usageRows = useMemo(() => rowsFromHash(usageQuery.data), [usageQuery.data]);
  const subscriptionRows = useMemo(() => rowsFromHash(subscriptionQuery.data), [subscriptionQuery.data]);
  const redeemRows = useMemo(() => rowsFromHash(redeemQuery.data), [redeemQuery.data]);
  const dataPushRows = useMemo(() => rowsFromHash(dataPushQuery.data), [dataPushQuery.data]);
  const supplyRows = useMemo(() => rowsFromHash(supplyQuery.data), [supplyQuery.data]);

  const totalBalance = accountRows.reduce((sum, account) => sum + Number(account.balance_cents ?? 0), 0);
  const totalSupplySeconds = supplyRows.reduce((sum, usage) => sum + Number(usage.used_seconds ?? 0), 0);
  const totalEarnings = supplyRows.reduce((sum, usage) => sum + Number(usage.earning_cents ?? 0), 0);
  const currentUsageCost = usageRows.reduce((sum, usage) => sum + Number(usage.stat_cost_cents ?? 0), 0);
  const currentDataPushDebit = dataPushRows.reduce((sum, usage) => sum + Number(usage.actual_debit_cents ?? 0), 0);

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
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: '时长', key: 'used_seconds', width: 100, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '实际扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '扣后余额', key: 'balance_after_cents', width: 120, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '创建时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

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
    if (view === 'data-push-usage') {
      return <Table columns={dataPushColumns} dataSource={dataPushRows} loading={dataPushQuery.loading} rowKey="key" size="small" scroll={{ x: 1300 }} />;
    }
    if (view === 'supply-usage') {
      return <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1300 }} />;
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
    'data-push-usage': '数据推送用量',
    'supply-usage': '供应事实',
  };

  const extra = (
    <Space>
      {view === 'accounts' && <Button type="primary" icon={<PlusOutlined />} onClick={openCreateAccount}>账号</Button>}
      {view === 'mount-point-groups' && <Button type="primary" icon={<PlusOutlined />} onClick={openCreateGroup}>分组</Button>}
      {view === 'mount-points' && <Button type="primary" icon={<PlusOutlined />} onClick={openMountPoint}>挂载点</Button>}
      {view === 'subscriptions' && <Button type="primary" icon={<PlusOutlined />} onClick={openSubscription}>订阅</Button>}
      {view === 'redeem-codes' && <Button type="primary" icon={<PlusOutlined />} onClick={openRedeemCode}>兑换码</Button>}
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
              <MetricCard title="供应时长" value={formatDuration(totalSupplySeconds)} prefix={<BranchesOutlined />} />
            </Col>
            <Col xs={12} md={6}>
              <MetricCard title="供应收益" value={formatCents(totalEarnings)} prefix={<WalletOutlined />} />
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
    </PageContainer>
  );
};

export default OperationsDashboard;
