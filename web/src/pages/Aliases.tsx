import React, { useState } from 'react';
import { Table, Button, Modal, Form, Input, Switch, AutoComplete, Typography, Space, Tag, Row, Col, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { useSSE } from '../hooks/useSSE';
import { aliasesApi } from '../api';
import type { AliasRule, ServerState } from '../api/types';

const { Title } = Typography;

const Aliases: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => aliasesApi.getAll(), 3000);
  const { data: servers } = useSSE<Record<string, ServerState>>('servers', { channels: 'servers' });
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<AliasRule | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const onlineMptOptions = servers
    ? [...new Set(Object.values(servers).map(s => s.login_mpt))].map(m => ({ value: m, label: m }))
    : [];

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ enable: true, visible: true });
    setModalOpen(true);
  };

  const handleEdit = (record: AliasRule) => {
    setEditing(record);
    form.setFieldsValue(record);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await aliasesApi.remove(uid);
      message.success('删除成功');
      refresh();
    } catch {
      message.error('删除失败');
    }
  };

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields();
      setSubmitting(true);
      if (editing) {
        await aliasesApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = `${values.source_name}_${values.alias_name}`;
        await aliasesApi.create(values);
        message.success('创建成功');
      }
      setModalOpen(false);
      refresh();
    } catch {
      message.error('操作失败');
    } finally {
      setSubmitting(false);
    }
  };

  const columns: ColumnsType<AliasRule & { key: string }> = [
    { title: '源挂载点', dataIndex: 'source_name', key: 'source_name', sorter: (a, b) => a.source_name.localeCompare(b.source_name) },
    { title: '别名', dataIndex: 'alias_name', key: 'alias_name' },
    {
      title: '启用', key: 'enable',
      render: (_, r) => <Tag color={r.enable ? 'green' : 'default'}>{r.enable ? '启用' : '禁用'}</Tag>,
    },
    {
      title: '可见', key: 'visible',
      render: (_, r) => <Tag color={r.visible ? 'blue' : 'default'}>{r.visible ? '可见' : '隐藏'}</Tag>,
    },
    {
      title: '操作', key: 'action', width: 140,
      render: (_, record) => (
        <Space>
          <Button type="link" size="small" icon={<EditOutlined />} onClick={() => handleEdit(record)}>编辑</Button>
          <Popconfirm title="确定删除？" onConfirm={() => handleDelete(record.uid)}>
            <Button type="link" size="small" danger icon={<DeleteOutlined />}>删除</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <Title level={4} style={{ margin: 0 }}>别名规则</Title>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增别名</Button>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
      />
      <Modal title={editing ? '编辑别名' : '新增别名'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting}>
        <Form form={form} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="source_name" label="源挂载点" rules={[{ required: true }]}>
                <AutoComplete
                  options={onlineMptOptions}
                  placeholder="选择在线挂载点"
                  disabled={!!editing}
                  filterOption={(input, option) =>
                    (option?.value ?? '').toLowerCase().includes(input.toLowerCase())
                  }
                />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="alias_name" label="别名" rules={[{ required: true }]}>
                <Input disabled={!!editing} />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="enable" label="启用" valuePropName="checked">
                <Switch />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="visible" label="可见" valuePropName="checked">
                <Switch />
              </Form.Item>
            </Col>
          </Row>
        </Form>
      </Modal>
    </div>
  );
};

export default Aliases;
