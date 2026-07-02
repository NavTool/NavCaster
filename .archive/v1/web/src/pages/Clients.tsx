import React, { useState, useEffect, useMemo } from 'react';
import { Table, Tag, Typography, Button, Popconfirm, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { UserOutlined, LogoutOutlined } from '@ant-design/icons';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { ClientState, StreamState } from '../api/types';
import { formatOnlineTime, formatQuality, formatBytes, formatSpeed } from '../utils/format';
import { useNavigate } from 'react-router-dom';
import { resourceApi } from '../api';

const { Title, Text } = Typography;

const qualityEntries = [
  { value: 0, text: '无' },
  { value: 1, text: '单点' },
  { value: 2, text: 'DGPS' },
  { value: 4, text: '固定解' },
  { value: 5, text: '浮动解' },
];

const Clients: React.FC = () => {
  const navigate = useNavigate();
  const { data, connected } = useSSE<Record<string, ClientState>>('clients');
  const { data: streams } = useSSE<Record<string, StreamState>>('streams');
  const loading = !connected && !data;
  const [, setTick] = useState(0);
  const [selectedRowKeys, setSelectedRowKeys] = useState<React.Key[]>([]);
  const [kicking, setKicking] = useState(false);

  // 1秒刷新在线时长
  useEffect(() => {
    const timer = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(timer);
  }, []);

  const streamsMap = useMemo(() => streams || {}, [streams]);

  const doKick = async (uid: string) => {
    try {
      await resourceApi.kickClient(uid);
      message.success(`已发送下线指令: ${uid}`);
    } catch (e) {
      message.error(`下线失败: ${(e as Error).message}`);
    }
  };

  const doBatchKick = async () => {
    if (selectedRowKeys.length === 0) return;
    setKicking(true);
    try {
      await Promise.all(selectedRowKeys.map(k => resourceApi.kickClient(String(k))));
      message.success(`已对 ${selectedRowKeys.length} 个用户发送下线指令`);
      setSelectedRowKeys([]);
    } catch (e) {
      message.error(`批量下线失败: ${(e as Error).message}`);
    } finally {
      setKicking(false);
    }
  };

  const cellStyle: React.CSSProperties = { fontSize: 13 };

  const columns: ColumnsType<ClientState & { key: string }> = [
    { title: '账号', dataIndex: 'account', key: 'account', width: 130,
      sorter: (a, b) => (a.account || '').localeCompare(b.account || ''),
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace', fontWeight: 600 }}>{v}</span>,
    },
    { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 130,
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: '使用挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 130,
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: 'IP', dataIndex: 'ip', key: 'ip', width: 130,
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace', color: '#8b90a8' }}>{v}</span>,
    },
    { title: '在线时长', key: 'online_time', width: 120,
      render: (_, r) => <span style={{ ...cellStyle, fontWeight: 500 }}>{formatOnlineTime(r.online_time)}</span>,
    },
    {
      title: '定位状态', key: 'quality', width: 90,
      render: (_, r) => {
        const q = formatQuality(r.quality);
        return <Tag style={{ borderRadius: 4, fontSize: 12 }} color={q.color}>{q.text}</Tag>;
      },
      filters: qualityEntries.map(e => ({ text: e.text, value: e.value })),
      onFilter: (value, record) => record.quality === value,
    },
    { title: '差分延迟', dataIndex: 'diff', key: 'diff', width: 90, align: 'right',
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{v ? `${v.toFixed(1)} s` : '-'}</span>,
    },
    { title: '发送带宽', key: 'send_speed', width: 110, align: 'right',
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{st ? formatSpeed(st.send_speed) : '-'}</span>;
      },
    },
    { title: '发送流量', key: 'send_total', width: 110, align: 'right',
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{st ? formatBytes(st.send_total) : '-'}</span>;
      },
    },
    { title: '操作', key: 'action', width: 100, fixed: 'right',
      render: (_, r) => (
        <Popconfirm
          title="确定将此用户强制下线?"
          description={r.account || r.uid}
          onConfirm={(e) => { e?.stopPropagation?.(); doKick(r.key); }}
          onCancel={(e) => e?.stopPropagation?.()}
          okText="确定"
          cancelText="取消"
        >
          <Button
            size="small"
            danger
            icon={<LogoutOutlined />}
            onClick={(e) => e.stopPropagation()}
          >强制下线</Button>
        </Popconfirm>
      ),
    },
  ];

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <UserOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
          <Title level={4} style={{ margin: 0 }}>移动站</Title>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          {selectedRowKeys.length > 0 && (
            <Space>
              <Text style={{ color: '#8b90a8', fontSize: 13 }}>已选 {selectedRowKeys.length} 项</Text>
              <Popconfirm
                title={`确定将选中的 ${selectedRowKeys.length} 个用户强制下线?`}
                onConfirm={doBatchKick}
                okText="确定"
                cancelText="取消"
              >
                <Button danger icon={<LogoutOutlined />} loading={kicking}>批量强制下线</Button>
              </Popconfirm>
            </Space>
          )}
          <StatusIndicator status="online" pulse size="sm" />
          <Text style={{ color: '#8b90a8', fontSize: 13 }}>{dataSource.length} 在线</Text>
        </div>
      </div>
      <div style={{ borderRadius: 8, border: '1px solid #2e3450', overflow: 'hidden' }}>
        <Table
          columns={columns}
          dataSource={dataSource}
          loading={loading}
          size="small"
          rowSelection={{
            selectedRowKeys,
            onChange: setSelectedRowKeys,
            preserveSelectedRowKeys: true,
          }}
          pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
          scroll={{ x: 1200 }}
          onRow={(record) => ({
            onClick: (e) => {
              const target = e.target as HTMLElement;
              if (target.closest('.ant-table-selection-column') || target.closest('.ant-btn') || target.closest('.ant-popover')) return;
              navigate(`/clients/${encodeURIComponent(record.key)}`);
            },
            style: { cursor: 'pointer' },
          })}
        />
      </div>
    </div>
  );
};

export default Clients;
