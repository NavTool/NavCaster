import React, { useCallback } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Descriptions } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import ConnectionHistoryTable from '../components/ConnectionHistoryTable';
import type { ServerState, StreamState } from '../api/types';
import { getMptHistory } from '../api';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed, getLocalTime } from '../utils/format';

const { Title } = Typography;

const ServerDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: servers } = useSSE<Record<string, ServerState>>('servers', { channels: 'servers' });
  const { data: streams } = useSSE<Record<string, StreamState>>('streams', { channels: 'streams' });
  const server = servers?.[id || ''];
  const stream = streams?.[id || ''];
  const mountName = server?.login_mpt || '';
  const fetchHistory = useCallback(() => getMptHistory(mountName), [mountName]);

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/servers">基准站</Link> },
        { title: server?.login_mpt || id || '基站详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{server?.login_mpt || id}</Title>

      {server ? (
        <>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="状态" valueRender={() => <StatusIndicator status="online" pulse label="在线" />} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="在线时长" value={formatOnlineTime(server.online_time)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="TCP 延迟" value={formatDelay(server.tcp_delay)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="速率" value={stream ? formatSpeed(stream.recv_speed || 0) : '-'} />
              </Card>
            </Col>
          </Row>
          <Card title="连接信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
            <Descriptions column={2} size="small">
              <Descriptions.Item label="挂载点">{server.login_mpt}</Descriptions.Item>
              <Descriptions.Item label="别名挂载点">{server.alias_mpt || '-'}</Descriptions.Item>
              <Descriptions.Item label="账号">{server.account}</Descriptions.Item>
              <Descriptions.Item label="IP">{server.ip}:{server.port}</Descriptions.Item>
              <Descriptions.Item label="上线时间">{getLocalTime(server.online_time)}</Descriptions.Item>
              <Descriptions.Item label="更新时间">{getLocalTime(server.update_time)}</Descriptions.Item>
            </Descriptions>
          </Card>
          {stream && (
            <Card title="流量统计" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
              <Row gutter={16}>
                <Col span={6}><Statistic title="发送总量" value={formatBytes(stream.send_total)} /></Col>
                <Col span={6}><Statistic title="发送速率" value={formatSpeed(stream.send_speed)} /></Col>
                <Col span={6}><Statistic title="接收总量" value={formatBytes(stream.recv_total)} /></Col>
                <Col span={6}><Statistic title="接收速率" value={formatSpeed(stream.recv_speed)} /></Col>
              </Row>
            </Card>
          )}
          <Card title="在线/离线记录" style={{ borderColor: '#2e3450' }}>
            {mountName ? <ConnectionHistoryTable fetchData={fetchHistory} showAccount /> : <Empty description="暂无数据" />}
          </Card>
        </>
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到基站 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default ServerDetail;
