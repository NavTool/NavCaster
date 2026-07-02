import React, { useEffect, useState, useCallback, useRef } from 'react';
import { Card, Select, InputNumber, Switch, Space, Button, Tag, message, Alert } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import PageContainer from '../components/PageContainer';
import { getRingLog, getSystemStatus, nodesApi } from '../api';
import type { RingLogEntry, CasterNode } from '../api/types';

const LEVEL_LABEL: Record<number, { label: string; color: string }> = {
  0: { label: 'TRACE', color: 'default' },
  1: { label: 'DEBUG', color: 'blue' },
  2: { label: 'INFO',  color: 'green' },
  3: { label: 'WARN',  color: 'gold' },
  4: { label: 'ERROR', color: 'red' },
  5: { label: 'CRIT',  color: 'magenta' },
};

const RingLog: React.FC = () => {
  const [items, setItems] = useState<RingLogEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [n, setN] = useState(500);
  const [level, setLevel] = useState<string>('info');
  const [autoRefresh, setAutoRefresh] = useState(false);
  const timerRef = useRef<number | null>(null);

  const [nodes, setNodes] = useState<CasterNode[]>([]);
  const [currentNodeId, setCurrentNodeId] = useState<string>('');
  const [selectedNode, setSelectedNode] = useState<string>('');
  const remote = selectedNode && currentNodeId && selectedNode !== currentNodeId;

  // 初始化：拉取当前节点 ID 与集群节点列表
  useEffect(() => {
    getSystemStatus().then((s: Record<string, unknown>) => {
      const nid = String(s.node_id || '');
      setCurrentNodeId(nid);
      setSelectedNode(prev => prev || nid);
    }).catch(() => {});
    nodesApi.getAll().then((m: Record<string, CasterNode>) => {
      setNodes(Object.values(m || {}));
    }).catch(() => {});
  }, []);

  const refresh = useCallback(async () => {
    if (remote) { setItems([]); return; }
    setLoading(true);
    try {
      const res = await getRingLog(n, level);
      setItems(res.items || []);
    } catch (e) {
      message.error('加载日志失败');
    } finally {
      setLoading(false);
    }
  }, [n, level, remote]);

  useEffect(() => { refresh(); }, [refresh]);

  useEffect(() => {
    if (timerRef.current) { window.clearInterval(timerRef.current); timerRef.current = null; }
    if (autoRefresh && !remote) {
      timerRef.current = window.setInterval(refresh, 5000);
    }
    return () => { if (timerRef.current) window.clearInterval(timerRef.current); };
  }, [autoRefresh, refresh, remote]);

  return (
    <PageContainer title="进程内日志" subtitle="环形缓冲区保存最近 5000 条 spdlog 输出，可作为故障现场的快速诊断手段。">
      <Card style={{ marginBottom: 12 }}>
        <Space wrap>
          <Space>
            <span>节点</span>
            <Select
              value={selectedNode || undefined}
              onChange={setSelectedNode}
              style={{ width: 220 }}
              placeholder="选择节点"
              options={nodes.map(n => ({
                value: n.uid,
                label: `${n.node_name || n.uid}${n.uid === currentNodeId ? ' (本节点)' : ''}`,
              }))}
            />
          </Space>
          <Space>
            <span>级别</span>
            <Select value={level} onChange={setLevel} style={{ width: 120 }}
              options={['trace','debug','info','warn','err','critical'].map(v => ({ value: v, label: v.toUpperCase() }))} />
          </Space>
          <Space>
            <span>条数</span>
            <InputNumber value={n} onChange={v => setN(Number(v) || 500)} min={50} max={5000} step={50} />
          </Space>
          <Space>
            <span>5s 自动刷新</span>
            <Switch checked={autoRefresh} onChange={setAutoRefresh} disabled={!!remote} />
          </Space>
          <Button icon={<ReloadOutlined />} onClick={refresh} loading={loading} disabled={!!remote}>刷新</Button>
        </Space>
      </Card>
      {remote && (
        <Alert
          type="info"
          showIcon
          style={{ marginBottom: 12 }}
          message="跨节点日志拉取尚未实现"
          description="进程环形日志仅保存在各节点自身内存，远端节点日志请直接登录该节点 Web，或查看集群事件 (LOG:NODE:*)。"
        />
      )}
      <Card bodyStyle={{ padding: 0, background: '#0e1024' }}>
        <div style={{ maxHeight: '70vh', overflow: 'auto', fontFamily: 'monospace', fontSize: 12, padding: 12 }}>
          {items.map((e, idx) => {
            const meta = LEVEL_LABEL[e.level] || LEVEL_LABEL[2];
            return (
              <div key={idx} style={{ whiteSpace: 'pre-wrap', color: '#cdd2e3', marginBottom: 2 }}>
                <Tag color={meta.color} style={{ marginRight: 6 }}>{meta.label}</Tag>
                <span>{e.message}</span>
              </div>
            );
          })}
          {items.length === 0 && !remote && (
            <div style={{ color: '#666', textAlign: 'center', padding: 24 }}>无日志</div>
          )}
        </div>
      </Card>
    </PageContainer>
  );
};

export default RingLog;
