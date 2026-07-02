import React, { useState, useEffect, useMemo } from 'react';
import { Table, Typography, Button, Popconfirm, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { CloudServerOutlined, LogoutOutlined } from '@ant-design/icons';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { ServerState, StreamState } from '../api/types';
import { formatOnlineTime, formatBytes, formatSpeed } from '../utils/format';
import { useNavigate } from 'react-router-dom';
import { usePolling } from '../hooks/usePolling';
import { resourceApi } from '../api';

const { Title, Text } = Typography;

const Servers: React.FC = () => {
  const navigate = useNavigate();
  const { data, connected } = useSSE<Record<string, ServerState>>('servers');
  const { data: streams } = useSSE<Record<string, StreamState>>('streams');
  const { data: subCounts } = usePolling(() => resourceApi.getMountpointSubscribers(), 5000);
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
      await resourceApi.kickServer(uid);
      message.success(`已发送下线指令: ${uid}`);
    } catch (e) {
      message.error(`下线失败: ${(e as Error).message}`);
    }
  };

  const doBatchKick = async () => {
    if (selectedRowKeys.length === 0) return;
    setKicking(true);
    try {
      await Promise.all(selectedRowKeys.map(k => resourceApi.kickServer(String(k))));
      message.success(`已对 ${selectedRowKeys.length} 个基站发送下线指令`);
      setSelectedRowKeys([]);
    } catch (e) {
      message.error(`批量下线失败: ${(e as Error).message}`);
    } finally {
      setKicking(false);
    }
  };

  const cellStyle: React.CSSProperties = { fontSize: 13 };

  const columns: ColumnsType<ServerState & { key: string }> = [
    { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 130,
      sorter: (a, b) => a.login_mpt.localeCompare(b.login_mpt),
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace', fontWeight: 600 }}>{v}</span>,
    },
    { title: '实际挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 130,
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: '账户', dataIndex: 'account', key: 'account', width: 110,
      render: (v) => <span style={cellStyle}>{v || 'SYSTEM'}</span>,
    },
    { title: 'IP', dataIndex: 'ip', key: 'ip', width: 130,
      render: (v) => <span style={{ ...cellStyle, fontFamily: 'monospace', color: '#8b90a8' }}>{v || '-'}</span>,
    },
    { title: '连接数', key: 'sub_count', width: 80, align: 'right',
      render: (_, r) => {
        const mpt = r.alias_mpt || r.login_mpt;
        const count = subCounts && mpt ? subCounts[mpt] : undefined;
        return <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{count !== undefined ? count : '-'}</span>;
      },
    },
    { title: '在线时长', key: 'online_time', width: 120,
      render: (_, r) => <span style={{ ...cellStyle, fontWeight: 500 }}>{formatOnlineTime(r.online_time)}</span>,
    },
    { title: '接收带宽', key: 'recv_speed', width: 110, align: 'right',
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{st ? formatSpeed(st.recv_speed) : '-'}</span>;
      },
    },
    { title: '接收流量', key: 'recv_total', width: 110, align: 'right',
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return <span style={{ ...cellStyle, fontFamily: 'monospace' }}>{st ? formatBytes(st.recv_total) : '-'}</span>;
      },
    },
    { title: '操作', key: 'action', width: 100, fixed: 'right',
      render: (_, r) => (
        <Popconfirm
          title="确定将此基站强制下线?"
          description={r.login_mpt || r.uid}
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
          <CloudServerOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
          <Title level={4} style={{ margin: 0 }}>基准站</Title>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          {selectedRowKeys.length > 0 && (
            <Space>
              <Text style={{ color: '#8b90a8', fontSize: 13 }}>已选 {selectedRowKeys.length} 项</Text>
              <Popconfirm
                title={`确定将选中的 ${selectedRowKeys.length} 个基站强制下线?`}
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
          scroll={{ x: 1100 }}
          onRow={(record) => ({
            onClick: (e) => {
              const target = e.target as HTMLElement;
              if (target.closest('.ant-table-selection-column') || target.closest('.ant-btn') || target.closest('.ant-popover')) return;
              navigate(`/servers/${encodeURIComponent(record.key)}`);
            },
            style: { cursor: 'pointer' },
          })}
        />
      </div>
    </div>
  );
};

export default Servers;
