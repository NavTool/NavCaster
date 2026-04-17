import React from 'react';
import { Table, Tag, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useSSE } from '../hooks/useSSE';
import type { ServerState } from '../api/types';
import { formatOnlineTime, getLocalTime } from '../utils/format';

const { Title } = Typography;

const columns: ColumnsType<ServerState & { key: string }> = [
  { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 120, sorter: (a, b) => a.login_mpt.localeCompare(b.login_mpt) },
  { title: '实际挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 120 },
  { title: '账户', dataIndex: 'account', key: 'account', width: 120 },
  { title: 'IP', dataIndex: 'ip', key: 'ip', width: 120 },
  { title: '端口', dataIndex: 'port', key: 'port', width: 80 },
  { title: '在线时长', key: 'online_time', width: 120, render: (_, r) => formatOnlineTime(r.online_time) },
  { title: 'ECEF X', dataIndex: 'ecef_x', key: 'ecef_x', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: 'ECEF Y', dataIndex: 'ecef_y', key: 'ecef_y', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: 'ECEF Z', dataIndex: 'ecef_z', key: 'ecef_z', width: 150, render: (v) => v ? v.toFixed(4) : '-' },
  { title: '数据更新时间', key: 'update_time', width: 180, render: (_, r) => getLocalTime(r.update_time) },
];

const Servers: React.FC = () => {
  const { data, connected } = useSSE<Record<string, ServerState>>('servers');
  const loading = !connected && !data;

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <Title level={4} style={{ margin: 0 }}>基准站</Title>
        <Tag color="blue">{dataSource.length} 在线</Tag>
      </div>
      <Table
        columns={columns}
        dataSource={dataSource}
        loading={loading}
        size="small"
        pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 1300 }}
      />
    </div>
  );
};

export default Servers;
