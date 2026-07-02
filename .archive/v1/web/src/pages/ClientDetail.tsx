import React, { useCallback } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Descriptions, Tag } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useSSE } from '../hooks/useSSE';
import ConnectionHistoryTable from '../components/ConnectionHistoryTable';
import type { ClientState, StreamState } from '../api/types';
import { getUsrHistory } from '../api';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed, getLocalTime, formatQuality, ecefToGeodetic, formatLatLon, formatHeight, formatEcef } from '../utils/format';

const { Title } = Typography;

const ClientDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: clients } = useSSE<Record<string, ClientState>>('clients', { channels: 'clients' });
  const { data: streams } = useSSE<Record<string, StreamState>>('streams', { channels: 'streams' });
  const client = clients?.[id || ''];
  const stream = streams?.[id || ''];
  const userName = client?.account || '';
  const fetchHistory = useCallback(() => getUsrHistory(userName), [userName]);

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
          {(() => {
            const geo = ecefToGeodetic(client.ecef_x, client.ecef_y, client.ecef_z);
            const q = formatQuality(client.quality);
            return (
              <Card title="定位与坐标信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
                <Descriptions column={2} size="small">
                  <Descriptions.Item label="定位状态">
                    <Tag color={q.color}>{q.text}</Tag>
                  </Descriptions.Item>
                  <Descriptions.Item label="卫星数">{client.sat_num || 0}</Descriptions.Item>
                  <Descriptions.Item label="差分延迟">{client.diff ? `${client.diff.toFixed(1)} s` : '-'}</Descriptions.Item>
                  <Descriptions.Item label="距基站距离">{client.distance ? `${(client.distance / 1000).toFixed(2)} km` : '-'}</Descriptions.Item>
                  {geo.valid ? (
                    <>
                      <Descriptions.Item label="纬度">{formatLatLon(geo.lat, 'lat')}</Descriptions.Item>
                      <Descriptions.Item label="经度">{formatLatLon(geo.lng, 'lng')}</Descriptions.Item>
                      <Descriptions.Item label="椭球高">{formatHeight(geo.height)}</Descriptions.Item>
                    </>
                  ) : (
                    <Descriptions.Item label="大地坐标" span={2}>无定位数据</Descriptions.Item>
                  )}
                  <Descriptions.Item label="ECEF X">{formatEcef(client.ecef_x)}</Descriptions.Item>
                  <Descriptions.Item label="ECEF Y">{formatEcef(client.ecef_y)}</Descriptions.Item>
                  <Descriptions.Item label="ECEF Z">{formatEcef(client.ecef_z)}</Descriptions.Item>
                  <Descriptions.Item label="位置更新时间" span={2}>
                    {client.position_update_time ? getLocalTime(client.position_update_time) : '-'}
                  </Descriptions.Item>
                </Descriptions>
              </Card>
            );
          })()}
          <Card title="连接信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
            <Descriptions column={2} size="small">
              <Descriptions.Item label="挂载点">{client.login_mpt}</Descriptions.Item>
              <Descriptions.Item label="别名挂载点">{client.alias_mpt || '-'}</Descriptions.Item>
              <Descriptions.Item label="账号">{client.account}</Descriptions.Item>
              <Descriptions.Item label="IP">{client.ip}:{client.port}</Descriptions.Item>
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
            {userName ? <ConnectionHistoryTable fetchData={fetchHistory} showMount /> : <Empty description="暂无数据" />}
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
