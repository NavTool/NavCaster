import React, { useState, useEffect } from 'react';
import { Table, Typography, Card, Row, Col, Tag, Button, Input, message, Descriptions, Space, Select, Tooltip } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { CloudDownloadOutlined, DeleteOutlined, HomeOutlined } from '@ant-design/icons';
import { usePolling } from '../hooks/usePolling';
import { useSSE } from '../hooks/useSSE';
import { sourcesApi, fetchRemoteSourcetable, fetchLocalSourcetable } from '../api';
import type { SourceRecord, ServerState } from '../api/types';
import { SourceRecordType, SourceDisplayType } from '../api/types';
import { getBaseURL } from '../api/client';
import type { SourcetableEntry } from '../api';
import api from '../api/client';

const { Title, Text } = Typography;

const recordTypeLabels: Record<number, { text: string; color: string }> = {
  [SourceRecordType.SOURCE_RECORD_TYPE_REAL]: { text: '实体', color: 'blue' },
  [SourceRecordType.SOURCE_RECORD_TYPE_NEAREST]: { text: '就近', color: 'green' },
  [SourceRecordType.SOURCE_RECORD_TYPE_ALIAS]: { text: '别名', color: 'orange' },
  [SourceRecordType.SOURCE_RECORD_TYPE_PROXY]: { text: '代理', color: 'purple' },
  [SourceRecordType.SOURCE_RECORD_TYPE_GRID]: { text: '网格', color: 'cyan' },
};

const displayTypeLabels: Record<number, string> = {
  [SourceDisplayType.SOURCE_DISP_TYPE_ALWAYS_SHOW]: '始终显示',
  [SourceDisplayType.SOURCE_DISP_TYPE_ALWAYS_HIDE]: '始终隐藏',
  [SourceDisplayType.SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE]: '在线时显示',
};

const SourceTable: React.FC = () => {
  const { data: sourcesData, loading } = usePolling(() => sourcesApi.getAll(), 5000);
  const { data: servers } = useSSE<Record<string, ServerState>>('servers', { channels: 'servers' });
  const [remoteEntries, setRemoteEntries] = useState<SourcetableEntry[]>([]);
  const [fetchingRemote, setFetchingRemote] = useState(false);
  const [ntripHost, setNtripHost] = useState('');
  const [ntripPort, setNtripPort] = useState(2101);
  const [ntripVersion, setNtripVersion] = useState<string>('2.0');

  // Try to get the NTRIP port from status API
  useEffect(() => {
    const base = getBaseURL();
    if (base) {
      try {
        const url = new URL(base);
        setNtripHost(url.hostname);
      } catch { /* ignore */ }
    }
    api.get('/api/status').then(({ data }) => {
      if (data?.ntrip_port) setNtripPort(data.ntrip_port);
    }).catch(() => {});
  }, []);

  const onlineMpts = new Set(servers ? Object.values(servers).map(s => s.alias_mpt || s.login_mpt) : []);

  const sources = sourcesData
    ? Object.entries(sourcesData).map(([key, val]) => ({ ...val, key }))
    : [];

  const configColumns: ColumnsType<SourceRecord & { key: string }> = [
    {
      title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 130,
      render: (v: string) => (
        <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>
          {v}
          {onlineMpts.has(v) && <Tag color="green" style={{ marginLeft: 6, fontSize: 10 }}>在线</Tag>}
        </span>
      ),
    },
    {
      title: '类型', key: 'record_type', width: 80,
      render: (_, r) => {
        const t = recordTypeLabels[r.record_type];
        return t ? <Tag color={t.color}>{t.text}</Tag> : '-';
      },
    },
    { title: '格式', dataIndex: 'format', key: 'format', width: 100 },
    { title: '格式详情', dataIndex: 'format_details', key: 'format_details', width: 120 },
    { title: '载波', dataIndex: 'carrier', key: 'carrier', width: 60 },
    { title: '卫星系统', dataIndex: 'nav_system', key: 'nav_system', width: 100 },
    { title: '国家', dataIndex: 'country', key: 'country', width: 60 },
    { title: '纬度', dataIndex: 'latitude', key: 'latitude', width: 80 },
    { title: '经度', dataIndex: 'longitude', key: 'longitude', width: 80 },
    {
      title: '显示', key: 'display_type', width: 100,
      render: (_, r) => displayTypeLabels[r.display_type] || '-',
    },
    { title: '网络', dataIndex: 'network', key: 'network', width: 80 },
    { title: '生成器', dataIndex: 'generator', key: 'generator', width: 100 },
  ];

  const remoteColumns: ColumnsType<SourcetableEntry & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', width: 130, render: (v: string) => <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>{v}</span> },
    { title: '标识', dataIndex: 'identifier', key: 'identifier', width: 120 },
    { title: '格式', dataIndex: 'format', key: 'format', width: 100 },
    { title: '格式详情', dataIndex: 'format_details', key: 'format_details', width: 120 },
    { title: '国家', dataIndex: 'country', key: 'country', width: 60 },
    { title: '纬度', dataIndex: 'latitude', key: 'latitude', width: 80 },
    { title: '经度', dataIndex: 'longitude', key: 'longitude', width: 80 },
  ];

  const handleFetchSelf = async () => {
    if (!ntripHost) { message.error('未设置服务器地址'); return; }
    setFetchingRemote(true);
    try {
      const entries = await fetchRemoteSourcetable(ntripHost, ntripPort, undefined, undefined, ntripVersion);
      setRemoteEntries(entries);
      if (entries.length === 0) message.info('源表为空');
      else message.success(`获取到 ${entries.length} 个挂载点`);
    } catch {
      message.error('获取源表失败，请检查 NTRIP 地址和端口');
    } finally {
      setFetchingRemote(false);
    }
  };

  const handleFetchLocal = async () => {
    setFetchingRemote(true);
    try {
      const entries = await fetchLocalSourcetable();
      setRemoteEntries(entries);
      if (entries.length === 0) message.info('源表为空');
      else message.success(`获取到 ${entries.length} 个挂载点`);
    } catch {
      message.error('获取本地源表失败');
    } finally {
      setFetchingRemote(false);
    }
  };

  return (
    <div>
      <Title level={4} style={{ marginBottom: 20 }}>源表管理</Title>

      <Row gutter={[16, 16]}>
        <Col span={24}>
          <Card
            title={<span>配置源表 <Text style={{ color: '#6b7194', fontSize: 13 }}>（自定义源表记录）</Text></span>}
            style={{ borderColor: '#2e3450' }}
          >
            <Descriptions size="small" column={3} style={{ marginBottom: 12 }}>
              <Descriptions.Item label="总记录数">{sources.length}</Descriptions.Item>
              <Descriptions.Item label="在线挂载点">{onlineMpts.size}</Descriptions.Item>
            </Descriptions>
            <Table
              columns={configColumns}
              dataSource={sources}
              loading={loading}
              size="small"
              pagination={{ pageSize: 50, showSizeChanger: true }}
              scroll={{ x: 1200 }}
            />
          </Card>
        </Col>
        <Col span={24}>
          <Card
            title={<span>实时源表 <Text style={{ color: '#6b7194', fontSize: 13 }}>（客户端实际获取的源表）</Text></span>}
            extra={
              <Space size="small" wrap>
                <Tooltip title="直接从本节点内部获取源表"><Button icon={<HomeOutlined />} onClick={handleFetchLocal} loading={fetchingRemote}>本地源表</Button></Tooltip>
                <Input
                  size="small"
                  style={{ width: 130 }}
                  placeholder="NTRIP 地址"
                  value={ntripHost}
                  onChange={e => setNtripHost(e.target.value)}
                />
                <Input
                  size="small"
                  style={{ width: 72 }}
                  placeholder="端口"
                  value={ntripPort}
                  onChange={e => setNtripPort(Number(e.target.value) || 2101)}
                />
                <Select size="small" style={{ width: 100 }} value={ntripVersion} onChange={setNtripVersion}
                  options={[{ label: 'Ntrip 1.0', value: '1.0' }, { label: 'Ntrip 2.0', value: '2.0' }]} />
                <Button icon={<CloudDownloadOutlined />} onClick={handleFetchSelf} loading={fetchingRemote}>远程获取</Button>
                {remoteEntries.length > 0 && (
                  <Tooltip title="清空源列表"><Button icon={<DeleteOutlined />} danger size="small" onClick={() => setRemoteEntries([])} /></Tooltip>
                )}
              </Space>
            }
            style={{ borderColor: '#2e3450' }}
          >
            {remoteEntries.length > 0 ? (
              <Table
                columns={remoteColumns}
                dataSource={remoteEntries.map((e, i) => ({ ...e, key: `${e.mountpoint}-${i}` }))}
                size="small"
                pagination={{ pageSize: 50 }}
                scroll={{ x: 800 }}
              />
            ) : (
              <div style={{ textAlign: 'center', padding: 32, color: '#6b7194' }}>
                <CloudDownloadOutlined style={{ fontSize: 32, marginBottom: 8 }} />
                <div>点击"刷新源表"按钮获取当前 Caster 对外发布的实时源表</div>
              </div>
            )}
          </Card>
        </Col>
      </Row>
    </div>
  );
};

export default SourceTable;
