import React, { useMemo, useState } from 'react';
import { Button, Popconfirm, Result, Select, Space, Table, Typography, message } from 'antd';
import { ArrowLeftOutlined, DeleteOutlined, PlusOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { useNavigate, useParams } from 'react-router-dom';
import { accessGroupsApi, accessItemsApi, accountsApi, sourcesApi } from '../api';
import { AccessState, SourceDecordType, SourceDisplayType } from '../api/types';
import type { AccessGroup, AccessItem, AccountRecord, SourceRecord } from '../api/types';
import { usePolling } from '../hooks/usePolling';

const { Title } = Typography;

type AccountRow = AccountRecord & { key: string };
type AccessItemRow = AccessItem & { key: string };
type AccessItemPolicyField = 'allow_visible' | 'allow_access' | 'allow_nearby';

const accessStateOptions = [
  { label: '默认', value: AccessState.ACCESS_STATE_DEFALT },
  { label: '允许', value: AccessState.ACCESS_STATE_ENABLE },
  { label: '禁止', value: AccessState.ACCESS_STATE_DISABLE },
];

const normalizeGroupUid = (groupUid?: string) => groupUid || 'default';

const AccessGroupDetail: React.FC = () => {
  const { id } = useParams();
  const groupUid = decodeURIComponent(id || '');
  const navigate = useNavigate();
  const [selectedUsers, setSelectedUsers] = useState<string[]>([]);
  const [selectedMounts, setSelectedMounts] = useState<string[]>([]);
  const [submitting, setSubmitting] = useState(false);

  const groupsState = usePolling(() => accessGroupsApi.getAll(), 3000, !!groupUid);
  const accountsState = usePolling(() => accountsApi.getAll(), 3000, !!groupUid);
  const accessItemsState = usePolling(() => accessItemsApi.getAll(groupUid), 3000, !!groupUid);
  const sourcesState = usePolling(() => sourcesApi.getAll(), 3000, !!groupUid);

  const group = groupsState.data?.[groupUid] as AccessGroup | undefined;

  const accounts = useMemo<AccountRow[]>(() => (
    Object.entries(accountsState.data || {}).map(([key, value]) => ({ ...value, uid: value.uid || key, key }))
  ), [accountsState.data]);

  const groupUsers = useMemo(() => (
    accounts.filter((account) => normalizeGroupUid(account.group_uid) === groupUid)
  ), [accounts, groupUid]);

  const availableUserOptions = useMemo(() => (
    accounts
      .filter((account) => normalizeGroupUid(account.group_uid) !== groupUid)
      .map((account) => ({ label: account.account || account.uid, value: account.uid }))
  ), [accounts, groupUid]);

  const accessItems = useMemo<AccessItemRow[]>(() => (
    Object.entries(accessItemsState.data || {}).map(([key, value]) => ({
      ...value,
      uid: value.uid || value.mount_point_name || key,
      mount_point_name: value.mount_point_name || key,
      key,
    }))
  ), [accessItemsState.data]);

  const accessItemMounts = useMemo(() => new Set(accessItems.map((item) => item.mount_point_name)), [accessItems]);

  const sourceMountOptions = useMemo(() => (
    Object.entries(sourcesState.data || {})
      .map(([key, source]: [string, SourceRecord]) => source.mountpoint || key)
      .filter((mount) => mount && !accessItemMounts.has(mount))
      .map((mount) => ({ label: mount, value: mount }))
  ), [sourcesState.data, accessItemMounts]);

  const refreshAll = () => {
    groupsState.refresh();
    accountsState.refresh();
    accessItemsState.refresh();
    sourcesState.refresh();
  };

  const handleAddUsers = async () => {
    if (selectedUsers.length === 0) return;
    setSubmitting(true);
    try {
      const selected = new Set(selectedUsers);
      const updates = accounts
        .filter((account) => selected.has(account.uid))
        .map((account) => accountsApi.update(account.uid, { ...account, group_uid: groupUid }));
      await Promise.all(updates);
      setSelectedUsers([]);
      message.success('用户已加入分组');
      refreshAll();
    } catch {
      message.error('用户更新失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleRemoveUser = async (account: AccountRow) => {
    setSubmitting(true);
    try {
      await accountsApi.update(account.uid, { ...account, group_uid: 'default' });
      message.success('用户已移出分组');
      refreshAll();
    } catch {
      message.error('用户更新失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleAddMounts = async () => {
    const mounts = Array.from(new Set(selectedMounts.map((mount) => mount.trim()).filter(Boolean)));
    if (mounts.length === 0) return;
    setSubmitting(true);
    try {
      await Promise.all(mounts.map((mount) => accessItemsApi.create(groupUid, {
        uid: mount,
        mount_point_name: mount,
        allow_visible: AccessState.ACCESS_STATE_DEFALT,
        allow_access: AccessState.ACCESS_STATE_DEFALT,
        allow_nearby: AccessState.ACCESS_STATE_DEFALT,
        decode_type: SourceDecordType.SOURCE_DECODE_TYPE_AUTO,
        display_type: SourceDisplayType.SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE,
      })));
      setSelectedMounts([]);
      message.success('挂载点已加入分组');
      refreshAll();
    } catch {
      message.error('挂载点更新失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleUpdateAccessItem = async (item: AccessItemRow, field: AccessItemPolicyField, value: AccessState) => {
    setSubmitting(true);
    try {
      await accessItemsApi.update(groupUid, { ...item, [field]: value });
      message.success('策略已更新');
      refreshAll();
    } catch {
      message.error('策略更新失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleRemoveAccessItem = async (item: AccessItemRow) => {
    setSubmitting(true);
    try {
      await accessItemsApi.remove(groupUid, item.mount_point_name);
      message.success('挂载点已移出分组');
      refreshAll();
    } catch {
      message.error('挂载点更新失败');
    } finally {
      setSubmitting(false);
    }
  };

  const userColumns: ColumnsType<AccountRow> = [
    { title: '账号', dataIndex: 'account', key: 'account' },
    { title: '联系人', dataIndex: 'contact_person', key: 'contact_person', width: 140 },
    { title: '备注', dataIndex: 'remark', key: 'remark' },
    {
      title: '操作', key: 'action', width: 90,
      render: (_, record) => (
        <Popconfirm title="确定移出？" onConfirm={() => handleRemoveUser(record)} disabled={groupUid === 'default'}>
          <Button type="link" size="small" danger icon={<DeleteOutlined />} disabled={groupUid === 'default'}>移出</Button>
        </Popconfirm>
      ),
    },
  ];

  const itemColumns: ColumnsType<AccessItemRow> = [
    { title: '挂载点', dataIndex: 'mount_point_name', key: 'mount_point_name' },
    {
      title: '可见', dataIndex: 'allow_visible', key: 'allow_visible', width: 120,
      render: (value: AccessState, record) => (
        <Select size="small" value={value} options={accessStateOptions} style={{ width: 88 }} onChange={(next) => handleUpdateAccessItem(record, 'allow_visible', next)} />
      ),
    },
    {
      title: '访问', dataIndex: 'allow_access', key: 'allow_access', width: 120,
      render: (value: AccessState, record) => (
        <Select size="small" value={value} options={accessStateOptions} style={{ width: 88 }} onChange={(next) => handleUpdateAccessItem(record, 'allow_access', next)} />
      ),
    },
    {
      title: '最近点', dataIndex: 'allow_nearby', key: 'allow_nearby', width: 120,
      render: (value: AccessState, record) => (
        <Select size="small" value={value} options={accessStateOptions} style={{ width: 88 }} onChange={(next) => handleUpdateAccessItem(record, 'allow_nearby', next)} />
      ),
    },
    {
      title: '操作', key: 'action', width: 90,
      render: (_, record) => (
        <Popconfirm title="确定移出？" onConfirm={() => handleRemoveAccessItem(record)}>
          <Button type="link" size="small" danger icon={<DeleteOutlined />}>移出</Button>
        </Popconfirm>
      ),
    },
  ];

  if (!groupUid) {
    return <Result status="404" title="访问分组不存在" />;
  }

  if (!groupsState.loading && !group) {
    return <Result status="404" title="访问分组不存在" extra={<Button onClick={() => navigate('/access')}>返回列表</Button>} />;
  }

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <Space>
          <Button icon={<ArrowLeftOutlined />} onClick={() => navigate('/access')} />
          <Title level={4} style={{ margin: 0 }}>{group?.group_name || groupUid}</Title>
        </Space>
      </div>

      <div style={{ marginBottom: 16 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'center', marginBottom: 12 }}>
          <Title level={5} style={{ margin: 0 }}>用户</Title>
          <Space.Compact style={{ width: 'min(560px, 100%)' }}>
            <Select
              mode="multiple"
              allowClear
              maxTagCount="responsive"
              placeholder="选择账号"
              value={selectedUsers}
              options={availableUserOptions}
              onChange={setSelectedUsers}
              style={{ flex: 1 }}
            />
            <Button type="primary" icon={<PlusOutlined />} loading={submitting} onClick={handleAddUsers}>加入</Button>
          </Space.Compact>
        </div>
        <Table
          columns={userColumns}
          dataSource={groupUsers}
          loading={accountsState.loading}
          size="small"
          pagination={{ pageSize: 10, showSizeChanger: true, showTotal: (total) => `共 ${total} 条` }}
          scroll={{ x: 720 }}
        />
      </div>

      <div>
        <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'center', marginBottom: 12 }}>
          <Title level={5} style={{ margin: 0 }}>挂载点</Title>
          <Space.Compact style={{ width: 'min(640px, 100%)' }}>
            <Select
              mode="tags"
              allowClear
              maxTagCount="responsive"
              tokenSeparators={[',', ';', ' ']}
              placeholder="选择或输入挂载点"
              value={selectedMounts}
              options={sourceMountOptions}
              onChange={setSelectedMounts}
              style={{ flex: 1 }}
            />
            <Button type="primary" icon={<PlusOutlined />} loading={submitting} onClick={handleAddMounts}>加入</Button>
          </Space.Compact>
        </div>
        <Table
          columns={itemColumns}
          dataSource={accessItems}
          loading={accessItemsState.loading || sourcesState.loading}
          size="small"
          pagination={{ pageSize: 12, showSizeChanger: true, showTotal: (total) => `共 ${total} 条` }}
          scroll={{ x: 760 }}
        />
      </div>
    </div>
  );
};

export default AccessGroupDetail;
