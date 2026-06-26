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
  DataPushUsage,
  HashRecord,
  MountPointRecord,
  OperationsAccount,
  RoleDashboard,
  StationRecord,
  SupplierEarningsSummary,
  SupplierSupplyUsage,
} from '../api/types';
import { currentPeriod, formatCents, formatDuration, getLocalTime } from '../utils/format';

type SelfScope = 'me' | 'supplier';
type UserView = 'dashboard' | 'profile' | 'access-accounts' | 'groups' | 'mount-points' | 'usage' | 'data-push';
type SupplierView = 'dashboard' | 'profile' | 'access-accounts' | 'stations' | 'supply-usage' | 'earnings';

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
  const [editing, setEditing] = useState<AccessAccountRecord | null>(null);
  const [modalOpen, setModalOpen] = useState(false);
  const [passwordTarget, setPasswordTarget] = useState<AccessAccountRecord | null>(null);

  const dashboardQuery = usePolling(() => api.dashboard(), 5000, view === 'dashboard');
  const profileQuery = usePolling(() => api.profile(), 5000, view === 'dashboard' || view === 'profile');
  const accessQuery = usePolling(() => api.accessAccounts(), 5000, view === 'dashboard' || view === 'access-accounts');
  const groupsQuery = usePolling(() => meApi.allowedGroups(), 5000, scope === 'me' && (view === 'dashboard' || view === 'groups' || view === 'access-accounts'));
  const mountsQuery = usePolling(() => meApi.mountPoints(), 5000, scope === 'me' && (view === 'dashboard' || view === 'mount-points'));
  const usageQuery = usePolling(() => meApi.usage(currentPeriod()), 5000, scope === 'me' && (view === 'dashboard' || view === 'usage'));
  const dataPushQuery = usePolling(() => meApi.dataPushUsage(currentPeriod()), 5000, scope === 'me' && (view === 'dashboard' || view === 'data-push'));
  const stationsQuery = usePolling(() => supplierApi.stations(), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'stations'));
  const supplyQuery = usePolling(() => supplierApi.supplyUsage(currentPeriod()), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'supply-usage' || view === 'earnings'));
  const earningsQuery = usePolling(() => supplierApi.earnings(currentPeriod()), 5000, scope === 'supplier' && (view === 'dashboard' || view === 'earnings'));

  const accessRows = useMemo(() => rowsFromHash(accessQuery.data), [accessQuery.data]);
  const groupRows = useMemo(() => rowsFromHash(groupsQuery.data), [groupsQuery.data]);
  const mountRows = useMemo(() => rowsFromHash(mountsQuery.data), [mountsQuery.data]);
  const usageRows = useMemo(() => rowsFromHash(usageQuery.data), [usageQuery.data]);
  const dataPushRows = useMemo(() => rowsFromHash(dataPushQuery.data), [dataPushQuery.data]);
  const stationRows = useMemo(() => rowsFromHash(stationsQuery.data), [stationsQuery.data]);
  const supplyRows = useMemo(() => rowsFromHash(supplyQuery.data), [supplyQuery.data]);

  const groupOptions = groupRows.map((grant) => ({
    value: grant.group_id,
    label: grant.group?.name ? `${grant.group.name} (${grant.group_id})` : grant.group_id,
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
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
  ];

  const dataPushColumns: ColumnsType<DataPushUsage & { key: string }> = [
    { title: 'Usage ID', dataIndex: 'usage_id', key: 'usage_id', width: 230 },
    { title: '目标挂载点', dataIndex: 'target_mountpoint', key: 'target_mountpoint', width: 160 },
    { title: '时长', key: 'used_seconds', width: 110, render: (_, row) => formatDuration(row.used_seconds ?? 0) },
    { title: '统计费用', key: 'stat_cost_cents', width: 120, render: (_, row) => formatCents(row.stat_cost_cents) },
    { title: '扣费', key: 'actual_debit_cents', width: 120, render: (_, row) => formatCents(row.actual_debit_cents) },
    { title: '扣后余额', key: 'balance_after_cents', width: 120, render: (_, row) => formatCents(row.balance_after_cents) },
    { title: '时间', key: 'create_time', width: 170, render: (_, row) => getLocalTime(row.create_time ?? 0) },
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
    if (scope === 'me' && view === 'data-push') {
      return <Table columns={dataPushColumns} dataSource={dataPushRows} loading={dataPushQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
    }
    if (scope === 'supplier' && view === 'stations') {
      return <Table columns={stationColumns} dataSource={stationRows} loading={stationsQuery.loading} rowKey="key" size="small" scroll={{ x: 900 }} />;
    }
    if (scope === 'supplier' && view === 'supply-usage') {
      return <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />;
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
      <Col xs={12} md={6}><MetricCard title="已结算" value={formatCents(summary?.settled_earning_cents)} prefix={<WalletOutlined />} /></Col>
      <Col xs={12} md={6}><MetricCard title="累计收益" value={formatCents(summary?.total_earning_cents)} prefix={<WalletOutlined />} /></Col>
      <Col span={24}>
        <Table columns={supplyColumns} dataSource={supplyRows} loading={supplyQuery.loading} rowKey="key" size="small" scroll={{ x: 1120 }} />
      </Col>
    </Row>
  );

  const extra = view === 'access-accounts'
    ? <Button type="primary" icon={<PlusOutlined />} onClick={openCreate}>{accessTitle}</Button>
    : undefined;

  const viewTitle = (() => {
    if (view === 'dashboard') return title;
    if (view === 'profile') return '账号资料';
    if (view === 'access-accounts') return accessTitle;
    if (view === 'groups') return '授权分组';
    if (view === 'mount-points') return '可用挂载点';
    if (view === 'usage') return '计费用量';
    if (view === 'data-push') return '数据推送';
    if (view === 'stations') return '供应站点';
    if (view === 'supply-usage') return '供应时长';
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
