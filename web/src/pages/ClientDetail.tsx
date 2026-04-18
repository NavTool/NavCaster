import React from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Descriptions, Tag } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useSSE } from '../hooks/useSSE';
import type { ClientState, StreamState } from '../api/types';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed, getLocalTime, formatQuality } from '../utils/format';

const { Title } = Typography;

const ClientDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: clients } = useSSE<Record<string, ClientState>>('clients', { channels: 'clients' });
  const { data: streams } = useSSE<Record<string, StreamState>>('streams', { channels: 'streams' });
  const client = clients?.[id || ''];
  const stream = streams?.[id || ''];

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/clients">移动站</Link> },
        { title: client?.account || id || '移动站详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{client?.account || id}</Title>

      {client ? (
        <>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="定位质量" valueRender={() => {
                  const q = formatQuality(client.quality);
                  return <Tag color={q.color}>{q.text}</Tag>;
                }} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="在线时长" value={formatOnlineTime(client.online_time)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="TCP 延迟" value={formatDelay(client.tcp_delay)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="卫星数" value={client.sat_num || 0} />
              </Card>
            </Col>
          </Row>
          <Card title="连接信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
            <Descriptions column={2} size="small">
              <Descriptions.Item label="挂载点">{client.login_mpt}</Descriptions.Item>
              <Descriptions.Item label="别名挂载点">{client.alias_mpt || '-'}</Descriptions.Item>
              <Descriptions.Item label="账号">{client.account}</Descriptions.Item>
              <Descriptions.Item label="IP">{client.ip}:{client.port}</Descriptions.Item>
              <Descriptions.Item label="差分龄期">{client.diff || 0}s</Descriptions.Item>
              <Descriptions.Item label="距基站距离">{client.distance ? `${(client.distance / 1000).toFixed(1)} km` : '-'}</Descriptions.Item>
              <Descriptions.Item label="上线时间">{getLocalTime(client.online_time)}</Descriptions.Item>
              <Descriptions.Item label="更新时间">{getLocalTime(client.update_time)}</Descriptions.Item>
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
            <Empty description="历史记录功能开发中" />
          </Card>
        </>
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到移动站 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default ClientDetail;
