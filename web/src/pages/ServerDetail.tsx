import React, { useCallback, useMemo } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Descriptions, Tabs, Table, Tag } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import ConnectionHistoryTable from '../components/ConnectionHistoryTable';
import type { ServerState, StreamState, ClientState } from '../api/types';
import { getMptHistory } from '../api';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed, getLocalTime } from '../utils/format';

const { Title } = Typography;

const ServerDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: servers } = useSSE<Record<string, ServerState>>('servers', { channels: 'servers' });
  const { data: streams } = useSSE<Record<string, StreamState>>('streams', { channels: 'streams' });
  const { data: clients } = useSSE<Record<string, ClientState>>('clients', { channels: 'clients' });
  const server = servers?.[id || ''];
  const stream = streams?.[id || ''];
  const mountName = server?.login_mpt || '';
  const fetchHistory = useCallback(() => getMptHistory(mountName), [mountName]);
  const subscribers = useMemo(() => {
    if (!clients || !mountName) return [];
    return Object.entries(clients)
      .filter(([, client]) => client.login_mpt === mountName || client.alias_mpt === mountName)
      .map(([, client]) => client);
  }, [clients, mountName]);

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/servers">基准站</Link> },
        { title: server?.login_mpt || id || '基站详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{server?.login_mpt || id}</Title>

      {server ? (
        <Tabs items={[
          {
            key: 'overview',
            label: '概览',
            children: (
              <>
                <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
                  <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="状态" valueRender={() => <StatusIndicator status="online" pulse label="在线" />} /></Card></Col>
                  <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="在线时长" value={formatOnlineTime(server.online_time)} /></Card></Col>
                  <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="TCP 延迟" value={formatDelay(server.tcp_delay)} /></Card></Col>
                  <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="订阅者" value={subscribers.length} /></Card></Col>
                </Row>
                <Card title="连接信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
                  <Descriptions column={2} size="small">
                    <Descriptions.Item label="挂载点">{server.login_mpt}</Descriptions.Item>
                    <Descriptions.Item label="别名挂载点">{server.alias_mpt || '-'}</Descriptions.Item>
                    <Descriptions.Item label="账号">{server.account}</Descriptions.Item>
                    <Descriptions.Item label="IP">{server.ip}:{server.port}</Descriptions.Item>
                    <Descriptions.Item label="连接类型">SERVER</Descriptions.Item>
                    <Descriptions.Item label="节点 UID">{server.uid}</Descriptions.Item>
                    <Descriptions.Item label="上线时间">{getLocalTime(server.online_time)}</Descriptions.Item>
                    <Descriptions.Item label="更新时间">{getLocalTime(server.update_time)}</Descriptions.Item>
                  </Descriptions>
                </Card>
              </>
            ),
          },
          {
            key: 'stream',
            label: '数据流',
            children: (
              <Card title="流量统计" style={{ borderColor: '#2e3450' }}>
                {stream ? (
                  <Row gutter={[16, 16]}>
                    <Col span={6}><Statistic title="发送总量" value={formatBytes(stream.send_total)} /></Col>
                    <Col span={6}><Statistic title="发送速率" value={formatSpeed(stream.send_speed)} /></Col>
                    <Col span={6}><Statistic title="接收总量" value={formatBytes(stream.recv_total)} /></Col>
                    <Col span={6}><Statistic title="接收速率" value={formatSpeed(stream.recv_speed)} /></Col>
                  </Row>
                ) : <Empty description="暂无流量数据" />}
              </Card>
            ),
          },
          {
            key: 'subscribers',
            label: '订阅者',
            children: (
              <Card title="当前订阅者" style={{ borderColor: '#2e3450' }}>
                <Table
                  dataSource={subscribers}
                  rowKey="uid"
                  size="small"
                  pagination={false}
                  locale={{ emptyText: '暂无订阅者' }}
                  columns={[
                    { title: '账号', dataIndex: 'account', key: 'account', render: (value: string, record: ClientState) => <Link to={`/clients/${encodeURIComponent(record.uid)}`}>{value}</Link> },
                    { title: '地址', key: 'addr', render: (_, record: ClientState) => `${record.ip}:${record.port}` },
                    { title: '挂载点', dataIndex: 'login_mpt', key: 'login_mpt' },
                    { title: '在线时长', key: 'online_time', render: (_, record: ClientState) => formatOnlineTime(record.online_time) },
                    { title: '质量', key: 'quality', render: (_, record: ClientState) => <Tag color={record.quality > 0 ? 'green' : 'default'}>{record.quality || 0}</Tag> },
                  ]}
                />
              </Card>
            ),
          },
          {
            key: 'history',
            label: '历史',
            children: (
              <Card title="在线/离线记录" style={{ borderColor: '#2e3450' }}>
                {mountName ? <ConnectionHistoryTable fetchData={fetchHistory} showAccount /> : <Empty description="暂无数据" />}
              </Card>
            ),
          },
        ]} />
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到基站 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default ServerDetail;
