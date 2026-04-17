import React from 'react';
import { Table, Tag, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { UserOutlined } from '@ant-design/icons';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { ClientState } from '../api/types';
import { formatOnlineTime, formatQuality, getLocalTime } from '../utils/format';

const { Title, Text } = Typography;

const qualityEntries = [
  { value: 0, text: '无' },
  { value: 1, text: '单点' },
  { value: 2, text: 'DGPS' },
  { value: 4, text: '固定解' },
  { value: 5, text: '浮动解' },
];

const columns: ColumnsType<ClientState & { key: string }> = [
  { title: '账号', dataIndex: 'account', key: 'account', width: 120,
    sorter: (a, b) => (a.account || '').localeCompare(b.account || ''),
    render: (v) => <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>{v}</span>,
  },
  { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 120,
    render: (v) => <span style={{ fontFamily: 'monospace' }}>{v}</span>,
  },
  { title: '使用挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 120,
    render: (v) => <span style={{ fontFamily: 'monospace' }}>{v}</span>,
  },
  { title: 'IP', dataIndex: 'ip', key: 'ip', width: 120,
    render: (v) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v}</span>,
  },
  { title: '端口', dataIndex: 'port', key: 'port', width: 80 },
  { title: '在线时长', key: 'online_time', width: 120,
    render: (_, r) => <span style={{ fontWeight: 500 }}>{formatOnlineTime(r.online_time)}</span>,
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
  { title: '差分延迟', dataIndex: 'diff', key: 'diff', width: 90, render: (v) => v ? `${v.toFixed(1)} s` : '-' },
  { title: 'ECEF X', dataIndex: 'ecef_x', key: 'ecef_x', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: 'ECEF Y', dataIndex: 'ecef_y', key: 'ecef_y', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: 'ECEF Z', dataIndex: 'ecef_z', key: 'ecef_z', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: '数据更新时间', key: 'update_time', width: 180,
    render: (_, r) => <span style={{ color: '#8b90a8' }}>{getLocalTime(r.update_time)}</span>,
  },
];

const Clients: React.FC = () => {
  const { data, connected } = useSSE<Record<string, ClientState>>('clients');
  const loading = !connected && !data;

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
          scroll={{ x: 1500 }}
        />
      </div>
    </div>
  );
};

export default Clients;
