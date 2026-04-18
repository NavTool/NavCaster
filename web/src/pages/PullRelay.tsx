import React, { useState, useEffect, useCallback } from 'react';
import { Table, Button, Modal, Form, Input, InputNumber, Select, AutoComplete, Typography, Space, Tag, Row, Col, message, Popconfirm, Tooltip } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined, CloudDownloadOutlined, PlayCircleOutlined, PauseCircleOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { pullRecordsApi, pullStatesApi, fetchRemoteSourcetable, relayStart, relayStop } from '../api';
import type { PullRecord, PullState } from '../api/types';
import { PullType } from '../api/types';
import { formatOnlineTime } from '../utils/format';

const { Title } = Typography;

const typeLabels: Record<number, string> = {
  [PullType.PULL_TYPE_NTRIP_1_0]: 'NTRIP 1.0',
  [PullType.PULL_TYPE_NTRIP_2_0]: 'NTRIP 2.0',
  [PullType.PULL_TYPE_TCP_CLIENT]: 'TCP Client',
  [PullType.PULL_TYPE_TCP_SERVER]: 'TCP Server',
};

const PullRelay: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => pullRecordsApi.getAll(), 3000);
  const { data: states, refresh: refreshStates } = usePolling(() => pullStatesApi.getAll(), 3000);
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<PullRecord | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);
  const [mptOptions, setMptOptions] = useState<{ value: string; label: string }[]>([]);
  const [fetchingMpts, setFetchingMpts] = useState(false);
  const [selectedRowKeys, setSelectedRowKeys] = useState<React.Key[]>([]);
  const [batchModalOpen, setBatchModalOpen] = useState(false);
  const [batchForm] = Form.useForm();
  const [batchMptOptions, setBatchMptOptions] = useState<{ value: string; label: string }[]>([]);
  const [batchFetchingMpts, setBatchFetchingMpts] = useState(false);
  const [batchSubmitting, setBatchSubmitting] = useState(false);
  const [, setTick] = useState(0);

  // 1秒刷新在线时长
  useEffect(() => {
    const timer = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(timer);
  }, []);

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ type: PullType.PULL_TYPE_NTRIP_1_0, target_port: 2101 });
    setMptOptions([]);
    setModalOpen(true);
  };

  const handleEdit = (record: PullRecord) => {
    setEditing(record);
    form.setFieldsValue(record);
    setMptOptions([]);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await pullRecordsApi.remove(uid);
      message.success('删除成功');
      refresh();
    } catch {
      message.error('删除失败');
    }
  };

  const handleStart = async (uid: string) => {
    try {
      await relayStart('pull', uid);
      message.success('启动成功');
      refresh();
      refreshStates();
    } catch {
      message.error('启动失败');
    }
  };

  const handleStop = async (uid: string) => {
    try {
      await relayStop('pull', uid);
      message.success('停止成功');
      refresh();
      refreshStates();
    } catch {
      message.error('停止失败');
    }
  };

  const handleFetchMountpoints = async () => {
    const ip = form.getFieldValue('target_ip');
    const port = form.getFieldValue('target_port');
    if (!ip) { message.warning('请先填写目标 IP'); return; }
    setFetchingMpts(true);
    try {
      const user = form.getFieldValue('target_account') || '';
      const pass = form.getFieldValue('target_password') || '';
      const entries = await fetchRemoteSourcetable(ip, port || 2101, user, pass);
      if (entries.length === 0) {
        message.info('未获取到挂载点');
        setMptOptions([]);
      } else {
        setMptOptions(entries.map(e => ({
          value: e.mountpoint,
          label: `${e.mountpoint}${e.format ? ' (' + e.format + ')' : ''}`,
        })));
        form.setFieldValue('target_mpt', undefined);
        message.success(`获取到 ${entries.length} 个挂载点`);
      }
    } catch {
      message.error('获取挂载点失败，请检查目标地址和端口');
    } finally {
      setFetchingMpts(false);
    }
  };

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields();
      setSubmitting(true);
      if (editing) {
        await pullRecordsApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = `${values.target_ip}:${values.target_port}/${values.target_mpt}`;
        await pullRecordsApi.create(values);
        message.success('创建成功');
      }
      setModalOpen(false);
      refresh();
    } catch (e: unknown) {
      if (e && typeof e === 'object' && 'errorFields' in e) return;
      message.error('操作失败');
    } finally {
      setSubmitting(false);
    }
  };

  const handleBatchDelete = async () => {
    if (selectedRowKeys.length === 0) return;
    try {
      await Promise.all(selectedRowKeys.map(k => pullRecordsApi.remove(String(k))));
      message.success(`已删除 ${selectedRowKeys.length} 条`);
      setSelectedRowKeys([]);
      refresh();
    } catch {
      message.error('批量删除失败');
    }
  };

  const handleBatchStart = async () => {
    if (selectedRowKeys.length === 0) return;
    try {
      let ok = 0;
      for (const k of selectedRowKeys) {
        try { await relayStart('pull', String(k)); ok++; } catch { /* skip */ }
      }
      message.success(`已启动 ${ok} 条`);
      setSelectedRowKeys([]);
      refresh();
      refreshStates();
    } catch {
      message.error('批量启动失败');
    }
  };

  const handleBatchStop = async () => {
    if (selectedRowKeys.length === 0) return;
    try {
      let ok = 0;
      for (const k of selectedRowKeys) {
        try { await relayStop('pull', String(k)); ok++; } catch { /* skip */ }
      }
      message.success(`已停止 ${ok} 条`);
      setSelectedRowKeys([]);
      refresh();
      refreshStates();
    } catch {
      message.error('批量停止失败');
    }
  };

  const handleBatchFetchMountpoints = async () => {
    const ip = batchForm.getFieldValue('target_ip');
    const port = batchForm.getFieldValue('target_port');
    if (!ip) { message.warning('请先填写目标 IP'); return; }
    setBatchFetchingMpts(true);
    try {
      const user = batchForm.getFieldValue('target_account') || '';
      const pass = batchForm.getFieldValue('target_password') || '';
      const entries = await fetchRemoteSourcetable(ip, port || 2101, user, pass);
      if (entries.length === 0) {
        message.info('未获取到挂载点');
        setBatchMptOptions([]);
      } else {
        setBatchMptOptions(entries.map(e => ({ value: e.mountpoint, label: e.mountpoint })));
        message.success(`获取到 ${entries.length} 个挂载点`);
      }
    } catch {
      message.error('获取挂载点失败');
    } finally {
      setBatchFetchingMpts(false);
    }
  };

  const handleBatchSubmit = async () => {
    try {
      const values = await batchForm.validateFields();
      const mpts: string[] = values.target_mpts || [];
      if (mpts.length === 0) { message.warning('请选择至少一个挂载点'); return; }
      setBatchSubmitting(true);
      let ok = 0;
      for (const mpt of mpts) {
        try {
          const record = { ...values, target_mpt: mpt, login_mpt: mpt, uid: `${values.target_ip}:${values.target_port}/${mpt}` };
          delete record.target_mpts;
          await pullRecordsApi.create(record);
          ok++;
        } catch { /* skip duplicates */ }
      }
      message.success(`批量添加完成，成功 ${ok} 条`);
      setBatchModalOpen(false);
      refresh();
    } catch (e: unknown) {
      if (e && typeof e === 'object' && 'errorFields' in e) return;
      message.error('批量添加失败');
    } finally {
      setBatchSubmitting(false);
    }
  };

  const getState = useCallback((uid: string): PullState | undefined => {
    return states ? states[uid] : undefined;
  }, [states]);

  const columns: ColumnsType<PullRecord & { key: string }> = [
    { title: '本地挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 120 },
    { title: '类型', key: 'type', width: 100, render: (_, r) => typeLabels[r.type] || '未知' },
    { title: '目标 IP', dataIndex: 'target_ip', key: 'target_ip', width: 120 },
    { title: '目标端口', dataIndex: 'target_port', key: 'target_port', width: 80 },
    { title: '目标挂载点', dataIndex: 'target_mpt', key: 'target_mpt', width: 120 },
    { title: '账户', dataIndex: 'target_account', key: 'target_account', width: 100 },
    {
      title: '运行状态', key: 'state', width: 90,
      render: (_, r) => {
        const st = getState(r.uid);
        if (!r.enabled) return <Tag color="default">已禁用</Tag>;
        if (st && st.state === 1) return <Tag color="green">运行中</Tag>;
        if (st) return <Tag color="red">已断开</Tag>;
        return <Tag color="blue">已启用</Tag>;
      },
    },
    {
      title: '在线时长', key: 'online_time', width: 120,
      render: (_, r) => {
        const st = getState(r.uid);
        return st && st.create_time ? formatOnlineTime(st.create_time) : '-';
      },
    },
    {
      title: '节点', key: 'node', width: 120,
      render: (_, r) => {
        const st = getState(r.uid);
        return st?.node_name || '-';
      },
    },
    {
      title: '操作', key: 'action', width: 200, fixed: 'right',
      render: (_, record) => {
        const st = getState(record.uid);
        const running = st && st.state === 1;
        return (
          <Space>
            {running ? (
              <Popconfirm title="确定停止？" onConfirm={() => handleStop(record.uid)}>
                <Button type="link" size="small" icon={<PauseCircleOutlined />}>停止</Button>
              </Popconfirm>
            ) : (
              <Button type="link" size="small" icon={<PlayCircleOutlined />} onClick={() => handleStart(record.uid)}>启动</Button>
            )}
            <Button type="link" size="small" icon={<EditOutlined />} onClick={() => handleEdit(record)}>编辑</Button>
            <Popconfirm title="确定删除？" onConfirm={() => handleDelete(record.uid)}>
              <Button type="link" size="small" danger icon={<DeleteOutlined />}>删除</Button>
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <Title level={4} style={{ margin: 0 }}>Pull 中继</Title>
        <Space>
          {selectedRowKeys.length > 0 && (
            <>
              <Popconfirm title={`确定删除选中的 ${selectedRowKeys.length} 条？`} onConfirm={handleBatchDelete}>
                <Button danger icon={<DeleteOutlined />}>批量删除 ({selectedRowKeys.length})</Button>
              </Popconfirm>
              <Button icon={<PlayCircleOutlined />} onClick={handleBatchStart}>批量启动</Button>
              <Popconfirm title={`确定停止选中的 ${selectedRowKeys.length} 条？`} onConfirm={handleBatchStop}>
                <Button icon={<PauseCircleOutlined />}>批量停止</Button>
              </Popconfirm>
            </>
          )}
          <Button icon={<PlusOutlined />} onClick={() => { batchForm.resetFields(); batchForm.setFieldsValue({ type: PullType.PULL_TYPE_NTRIP_1_0, target_port: 2101 }); setBatchMptOptions([]); setBatchModalOpen(true); }}>批量添加</Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增 Pull</Button>
        </Space>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        rowSelection={{ selectedRowKeys, onChange: setSelectedRowKeys }}
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 1200 }}
      />
      <Modal title={editing ? '编辑 Pull' : '新增 Pull'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting} width={560}>
        <Form form={form} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="login_mpt" label="本地挂载点" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="type" label="协议类型">
                <Select options={Object.entries(typeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="target_ip" label="目标 IP" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="target_port" label="目标端口" rules={[{ required: true }]}>
                <InputNumber min={1} max={65535} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="target_account" label="账户">
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="target_password" label="密码">
                <Input.Password />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item name="target_mpt" label="目标挂载点" style={{ marginBottom: 0 }}>
            <AutoComplete
              options={mptOptions}
              placeholder="输入或从远端获取"
              filterOption={(input, option) =>
                (option?.value ?? '').toLowerCase().includes(input.toLowerCase())
              }
            >
              <Input
                suffix={
                  <Tooltip title="从目标服务器获取挂载点列表">
                    <CloudDownloadOutlined
                      onClick={handleFetchMountpoints}
                      style={{ cursor: 'pointer', color: fetchingMpts ? '#999' : '#1890ff' }}
                    />
                  </Tooltip>
                }
              />
            </AutoComplete>
          </Form.Item>
        </Form>
      </Modal>
      <Modal title="批量添加 Pull" open={batchModalOpen} onOk={handleBatchSubmit}
        onCancel={() => setBatchModalOpen(false)} confirmLoading={batchSubmitting} width={560}>
        <Form form={batchForm} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="target_ip" label="目标 IP" rules={[{ required: true }]}>
                <Input />
              </Form.Item>
            </Col>
            <Col span={6}>
              <Form.Item name="target_port" label="目标端口" rules={[{ required: true }]}>
                <InputNumber min={1} max={65535} style={{ width: '100%' }} />
              </Form.Item>
            </Col>
            <Col span={6}>
              <Form.Item name="type" label="协议类型">
                <Select options={Object.entries(typeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="target_account" label="账户">
                <Input />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="target_password" label="密码">
                <Input.Password />
              </Form.Item>
            </Col>
          </Row>
          <Form.Item label={<Space>选择挂载点<Button size="small" type="link" loading={batchFetchingMpts} icon={<CloudDownloadOutlined />} onClick={handleBatchFetchMountpoints}>获取列表</Button></Space>} name="target_mpts" rules={[{ required: true, message: '请选择挂载点' }]} style={{ marginBottom: 0 }}>
            <Select mode="multiple" placeholder="先获取列表，再选择要添加的挂载点" options={batchMptOptions} />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
};

export default PullRelay;
