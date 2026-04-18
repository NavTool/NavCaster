import React, { useEffect, useState, useCallback } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Segmented, Tabs, Descriptions, Tag, Button, Select, Switch, message, Spin, Table, Space } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useMultiSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import ConnectionHistoryTable from '../components/ConnectionHistoryTable';
import type { CasterNode } from '../api/types';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed } from '../utils/format';
import { getNodeHistory, getNodeConfig, getNodeServerHistory, getNodeClientHistory, getNodeRuntimeLogs, postNodeAction, type NodeHistorySnapshot, type NodeConfigInfo, type NodeRuntimeLogItem } from '../api';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend } from 'recharts';

const { Title } = Typography;

const TIME_RANGES = [
  { label: '1小时', value: 720, range: 'raw' as const },
  { label: '6小时', value: 4320, range: 'raw' as const },
  { label: '24小时', value: 17280, range: 'raw' as const },
  { label: '7天', value: 120960, range: 'raw' as const },
  { label: '30天', value: 33120, range: '1m' as const },
  { label: '1年', value: 96480, range: '5m' as const },
];

function formatTime(ts: number, longRange?: boolean): string {
  const d = new Date(ts * 1000);
  if (longRange) {
    return `${String(d.getMonth() + 1).padStart(2, '0')}/${String(d.getDate()).padStart(2, '0')} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  }
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

function formatTimestamp(ts: number): string {
  if (!ts) return '-';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

const chartCardStyle = { borderColor: '#2e3450', marginTop: 16 };
const chartHeight = 220;

const NodeDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: sseData } = useMultiSSE<{ nodes: Record<string, CasterNode> }>(['nodes']);
  const node = sseData.nodes?.[id || ''];

  const [history, setHistory] = useState<NodeHistorySnapshot[]>([]);
  const [selectedRange, setSelectedRange] = useState(0); // index into TIME_RANGES
  const [activeTab, setActiveTab] = useState('overview');

  // Config state
  const [nodeConfig, setNodeConfig] = useState<NodeConfigInfo | null>(null);
  const [configLoading, setConfigLoading] = useState(false);
  const [actionLoading, setActionLoading] = useState(false);
  const [runtimeLogs, setRuntimeLogs] = useState<NodeRuntimeLogItem[]>([]);
  const [runtimeLogsLoading, setRuntimeLogsLoading] = useState(false);
  const [runtimeLogLevel, setRuntimeLogLevel] = useState('info');

  const currentRange = TIME_RANGES[selectedRange];

  const loadHistory = useCallback(async () => {
    if (!id) return;
    try {
      const r = TIME_RANGES[selectedRange];
      const data = await getNodeHistory(id, r.value, r.range);
      // API returns newest-first (LPUSH), reverse for chronological order
      setHistory([...data].reverse());
    } catch { /* ignore */ }
  }, [id, selectedRange]);

  useEffect(() => {
    loadHistory();
    // 短时间范围(raw)5秒刷新, 长时间范围降低刷新频率
    const interval = currentRange.range === 'raw' ? 5000 : 30000;
    const timer = setInterval(loadHistory, interval);
    return () => clearInterval(timer);
  }, [loadHistory, currentRange.range]);

  const isLongRange = currentRange.range !== 'raw' || currentRange.value > 17280;

  // Load config when switching to config tab
  const loadConfig = useCallback(async () => {
    if (!id) return;
    setConfigLoading(true);
    try {
      const data = await getNodeConfig(id);
      setNodeConfig(data);
    } catch { /* ignore */ }
    finally { setConfigLoading(false); }
  }, [id]);

  useEffect(() => {
    if (activeTab === 'config') loadConfig();
  }, [activeTab, loadConfig]);

  const loadRuntimeLogs = useCallback(async () => {
    if (!id) return;
    setRuntimeLogsLoading(true);
    try {
      const data = await getNodeRuntimeLogs(id, { limit: 100, level: runtimeLogLevel });
      setRuntimeLogs(data.items ?? []);
    } catch {
      message.error('加载节点日志失败');
    } finally {
      setRuntimeLogsLoading(false);
    }
  }, [id, runtimeLogLevel]);

  useEffect(() => {
    if (activeTab !== 'logs') return;
    loadRuntimeLogs();
    const timer = setInterval(loadRuntimeLogs, 5000);
    return () => clearInterval(timer);
  }, [activeTab, loadRuntimeLogs]);

  const handleAction = async (action: string, params?: Record<string, unknown>) => {
    if (!id) return;
    setActionLoading(true);
    try {
      await postNodeAction(id, action, params);
      message.success(`操作 "${action}" 已发送`);
      if (action === 'config_update') loadConfig();
    } catch {
      message.error('操作失败');
    } finally {
      setActionLoading(false);
    }
  };

  const renderConfigTab = () => {
    if (configLoading) return <Spin />;
    if (!nodeConfig) return <Empty description="加载配置失败" />;

    return (
      <>
        <Card title="节点信息" style={{ borderColor: '#2e3450', marginBottom: 16 }}>
          <Descriptions column={2}>
            <Descriptions.Item label="节点 ID">{nodeConfig.node_id}</Descriptions.Item>
            <Descriptions.Item label="节点名称">{nodeConfig.node_name}</Descriptions.Item>
            <Descriptions.Item label="设定版本">{nodeConfig.set_version}</Descriptions.Item>
            <Descriptions.Item label="标签版本">{nodeConfig.tag_version}</Descriptions.Item>
            <Descriptions.Item label="运行平台">{nodeConfig.runtime.run_platform || '-'}</Descriptions.Item>
            <Descriptions.Item label="监听端口">{nodeConfig.runtime.listen_port || '-'}</Descriptions.Item>
            <Descriptions.Item label="HTTP 端口">{nodeConfig.runtime.http_port || '-'}</Descriptions.Item>
            <Descriptions.Item label="进程 ID">{nodeConfig.runtime.process_id || '-'}</Descriptions.Item>
            <Descriptions.Item label="上线时间">{formatTimestamp(nodeConfig.runtime.online_time)}</Descriptions.Item>
            <Descriptions.Item label="最近更新">{formatTimestamp(nodeConfig.runtime.update_time)}</Descriptions.Item>
          </Descriptions>
        </Card>

        <Card title="核心配置 (可热更新)" style={{ borderColor: '#2e3450', marginBottom: 16 }}>
          <Descriptions column={2} bordered size="small">
            {Object.entries(nodeConfig.core).map(([key, value]) => {
              const schemaKey = `core.${key}`;
              const schema = nodeConfig.schema[schemaKey];
              return (
                <Descriptions.Item key={key} label={schema?.label || key}>
                  {typeof value === 'boolean' ? (
                    <Switch size="small" checked={value}
                      onChange={(checked) => handleAction('config_update', { section: 'core', key, value: checked })} />
                  ) : (
                    <>{String(value)}</>
                  )}
                  {schema?.restart_required && <Tag color="orange" style={{ marginLeft: 8 }}>需重启</Tag>}
                </Descriptions.Item>
              );
            })}
          </Descriptions>
        </Card>

        <Card title="服务配置" style={{ borderColor: '#2e3450', marginBottom: 16 }}>
          <Descriptions column={2} bordered size="small">
            {Object.entries(nodeConfig.service).map(([key, value]) => {
              const schemaKey = `service.${key}`;
              const schema = nodeConfig.schema[schemaKey];
              return (
                <Descriptions.Item key={key} label={schema?.label || key}>
                  {typeof value === 'boolean' ? (
                    <Tag color={value ? 'green' : 'default'}>{value ? '启用' : '禁用'}</Tag>
                  ) : (
                    <>{String(value)}</>
                  )}
                  {schema?.restart_required && <Tag color="orange" style={{ marginLeft: 8 }}>需重启</Tag>}
                </Descriptions.Item>
              );
            })}
          </Descriptions>
        </Card>

        <Card title="节点控制" style={{ borderColor: '#2e3450' }}>
          <Row gutter={[16, 16]}>
            <Col>
              <Button onClick={() => handleAction('sync_cluster')} loading={actionLoading}>
                同步集群状态
              </Button>
            </Col>
            <Col>
              <Select placeholder="设置日志级别" style={{ width: 160 }}
                onChange={(level: string) => handleAction('set_log_level', { level })}
                options={[
                  { label: 'Trace', value: 'trace' },
                  { label: 'Debug', value: 'debug' },
                  { label: 'Info', value: 'info' },
                  { label: 'Warning', value: 'warn' },
                  { label: 'Error', value: 'error' },
                ]} />
            </Col>
          </Row>
        </Card>
      </>
    );
  };

  const renderConnectionHistoryTab = () => {
    if (!id) return <Empty description="未找到节点 ID" />;

    return (
      <Tabs
        items={[
          {
            key: 'server-history',
            label: '基站连接历史',
            children: (
              <ConnectionHistoryTable
                fetchData={() => getNodeServerHistory(id)}
                showAccount
              />
            ),
          },
          {
            key: 'client-history',
            label: '用户连接历史',
            children: (
              <ConnectionHistoryTable
                fetchData={() => getNodeClientHistory(id)}
                showMount
                showAccount
              />
            ),
          },
        ]}
      />
    );
  };

  const renderLogsTab = () => {
    if (!id) return <Empty description="未找到节点 ID" />;

    return (
      <Card
        title="节点运行日志"
        style={{ borderColor: '#2e3450' }}
        extra={(
          <Space>
            <Select
              value={runtimeLogLevel}
              style={{ width: 160 }}
              onChange={setRuntimeLogLevel}
              options={[
                { label: 'Info 及以上', value: 'info' },
                { label: 'Debug 及以上', value: 'debug' },
                { label: 'Warning 及以上', value: 'warn' },
                { label: 'Error 及以上', value: 'error' },
              ]}
            />
            <Button onClick={() => loadRuntimeLogs()} loading={runtimeLogsLoading}>刷新</Button>
          </Space>
        )}
      >
        <Table<NodeRuntimeLogItem>
          dataSource={runtimeLogs}
          rowKey={(record) => `${record.ts}-${record.message}`}
          loading={runtimeLogsLoading}
          size="small"
          pagination={{ pageSize: 20, hideOnSinglePage: true }}
          locale={{ emptyText: '暂无日志' }}
          columns={[
            { title: '时间', dataIndex: 'timestamp', key: 'timestamp', width: 200 },
            {
              title: '级别',
              dataIndex: 'level',
              key: 'level',
              width: 100,
              render: (value: string) => {
                const color = value === 'error' || value === 'critical'
                  ? 'red'
                  : value === 'warn'
                    ? 'orange'
                    : value === 'debug'
                      ? 'blue'
                      : 'green';
                return <Tag color={color}>{value.toUpperCase()}</Tag>;
              },
            },
            { title: 'Logger', dataIndex: 'logger', key: 'logger', width: 120 },
            { title: '消息', dataIndex: 'message', key: 'message' },
          ]}
        />
      </Card>
    );
  };

  const chartData = history.map(s => ({
    time: formatTime(s.t, isLongRange),
    cpu: +s.cpu.toFixed(1),
    mem: +(s.mem / 1024 / 1024).toFixed(1),
    mpt: s.mpt,
    usr: s.usr,
    send: +(s.send_speed / 1024).toFixed(1),
    recv: +(s.recv_speed / 1024).toFixed(1),
    delay: s.q_delay,
  }));

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/dashboard">节点状态</Link> },
        { title: node?.node_name || id || '节点详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{node?.node_name || id}</Title>

      {node ? (
        <Tabs activeKey={activeTab} onChange={setActiveTab} items={[
          {
            key: 'overview',
            label: '概览',
            children: (
              <>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="状态" valueRender={() => <StatusIndicator status="online" pulse label="在线" />} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="CPU" value={`${(node.cpu_usage || 0).toFixed(1)}%`} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="内存" value={formatBytes(node.mem_usage || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="运行时长" value={formatOnlineTime(node.online_time || 0)} />
              </Card>
            </Col>
          </Row>
          <Row gutter={[16, 16]}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="基站数" value={node.server_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="移动站数" value={node.client_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输入" value={formatSpeed(node.recv_speed || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输出" value={formatSpeed(node.send_speed || 0)} />
              </Card>
            </Col>
          </Row>
          <Card title="延迟信息" style={{ marginTop: 16, borderColor: '#2e3450' }}>
            <Row gutter={16}>
              <Col span={8}><Statistic title="处理延迟" value={formatDelay(node.queue_delay || 0)} /></Col>
              <Col span={8}><Statistic title="PUB TCP" value={formatDelay(node.pub_tcp_delay || 0)} /></Col>
              <Col span={8}><Statistic title="SUB TCP" value={formatDelay(node.sub_tcp_delay || 0)} /></Col>
            </Row>
          </Card>

          <Card title="历史趋势" style={chartCardStyle}
            extra={<Segmented options={TIME_RANGES.map((r, i) => ({ label: r.label, value: i }))}
              value={selectedRange} onChange={v => setSelectedRange(v as number)} size="small" />}>
            {chartData.length > 0 ? (
              <>
                <Row gutter={16}>
                  <Col span={12}>
                    <Card size="small" title="CPU (%)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="cpu" stroke="#4a8eff" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col span={12}>
                    <Card size="small" title="内存 (MB)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="mem" stroke="#52c41a" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                </Row>
                <Row gutter={16}>
                  <Col span={12}>
                    <Card size="small" title="连接数" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="mpt" name="基站" stroke="#4a8eff" dot={false} strokeWidth={1.5} />
                          <Line type="monotone" dataKey="usr" name="用户" stroke="#faad14" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col span={12}>
                    <Card size="small" title="流量 (KB/s)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="recv" name="接收" stroke="#52c41a" dot={false} strokeWidth={1.5} />
                          <Line type="monotone" dataKey="send" name="发送" stroke="#ff4d4f" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                </Row>
              </>
            ) : (
              <Empty description="暂无历史数据，数据每分钟采集一次" />
            )}
          </Card>
              </>
            ),
          },
          {
            key: 'config',
            label: '配置与控制',
            children: renderConfigTab(),
          },
          {
            key: 'connections',
            label: '连接历史',
            children: renderConnectionHistoryTab(),
          },
          {
            key: 'logs',
            label: '运行日志',
            children: renderLogsTab(),
          },
        ]} />
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到节点 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default NodeDetail;
