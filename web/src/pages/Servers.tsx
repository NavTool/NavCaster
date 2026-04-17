import React, { useState, useEffect, useMemo } from 'react';
import { Table, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { CloudServerOutlined } from '@ant-design/icons';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { ServerState, StreamState } from '../api/types';
import { formatOnlineTime, formatBytes, formatSpeed } from '../utils/format';

const { Title, Text } = Typography;

const Servers: React.FC = () => {
  const { data, connected } = useSSE<Record<string, ServerState>>('servers');
  const { data: streams } = useSSE<Record<string, StreamState>>('streams');
  const loading = !connected && !data;
  const [, setTick] = useState(0);

  // 1秒刷新在线时长
  useEffect(() => {
    const timer = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(timer);
  }, []);

  const streamsMap = useMemo(() => streams || {}, [streams]);

  const columns: ColumnsType<ServerState & { key: string }> = [
    { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 120,
      sorter: (a, b) => a.login_mpt.localeCompare(b.login_mpt),
      render: (v) => <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>{v}</span>,
    },
    { title: '实际挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 120,
      render: (v) => <span style={{ fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: '账户', dataIndex: 'account', key: 'account', width: 100,
      render: (v) => v || 'SYSTEM',
    },
    { title: 'IP', dataIndex: 'ip', key: 'ip', width: 120,
      render: (v) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v || '-'}</span>,
    },
    { title: '端口', dataIndex: 'port', key: 'port', width: 80,
      render: (v) => v || '-',
    },
    { title: '在线时长', key: 'online_time', width: 120,
      render: (_, r) => <span style={{ fontWeight: 500 }}>{formatOnlineTime(r.online_time)}</span>,
    },
    { title: '接收带宽', key: 'recv_speed', width: 120,
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return st ? formatSpeed(st.recv_speed) : '-';
      },
    },
    { title: '接收流量', key: 'recv_total', width: 120,
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return st ? formatBytes(st.recv_total) : '-';
      },
    },
    { title: 'ECEF X', dataIndex: 'ecef_x', key: 'ecef_x', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
    { title: 'ECEF Y', dataIndex: 'ecef_y', key: 'ecef_y', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
    { title: 'ECEF Z', dataIndex: 'ecef_z', key: 'ecef_z', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
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
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
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
          pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
          scroll={{ x: 1400 }}
        />
      </div>
    </div>
  );
};

export default Servers;
