import { Link } from 'react-router-dom';
import { ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Empty, Input, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { RuntimeSummary } from '../../api/contracts';
import { StatusBadge } from '../components/StatusBadge';
import { ControlPaginationBar } from '../components/TablePage';
import { formatDateTime } from './useControlPage';
import {
  buildBaseStations,
  buildMobileStations,
  humanBps,
  humanDuration,
  type BaseStationRow,
  type MobileStationRow,
} from './accessMonitorModel';

export default function AccessMonitorPage({ mode }: { mode: 'base' | 'mobile' }) {
  const [runtimes, setRuntimes] = useState<RuntimeSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [search, setSearch] = useState('');
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(10);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all' });
      setRuntimes(result.items);
      setError('');
    } catch (err) {
      setRuntimes([]);
      setError(err instanceof Error ? err.message : '接入监控数据加载失败');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const baseRows = useMemo(() => buildBaseStations(runtimes), [runtimes]);
  const mobileRows = useMemo(() => buildMobileStations(runtimes), [runtimes]);
  const query = search.trim().toLowerCase();
  const visibleBaseRows = baseRows.filter((row) => !query || [row.stationName, row.mountPoint, row.sourceAccount, row.runtimeName, row.hostName].some((item) => item.toLowerCase().includes(query)));
  const visibleMobileRows = mobileRows.filter((row) => !query || [row.accessAccount, row.displayName, row.ownerAccount, row.mountPoint, row.runtimeName, row.hostName].some((item) => item.toLowerCase().includes(query)));

  const baseColumns: ColumnsType<BaseStationRow> = [
    {
      title: '基准站',
      dataIndex: 'stationName',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/access-monitor/base-stations/${row.id}`}>{row.stationName}</Link>
          <span>{row.mountPoint}</span>
        </div>
      ),
    },
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '分组', dataIndex: 'groupName' },
    { title: '源站账号', dataIndex: 'sourceAccount' },
    { title: '当前客户端', dataIndex: 'clientCount', align: 'right' },
    { title: '源站上行', dataIndex: 'recvBps', render: humanBps },
    { title: '下行播发', dataIndex: 'sendBps', render: humanBps },
    { title: '所在节点', dataIndex: 'runtimeName', render: (_, row) => <Link to={`/admin/control/runtimes/${row.runtimeId}`}>{row.runtimeName}</Link> },
    { title: '所在实例', dataIndex: 'hostName' },
    { title: '源列表', dataIndex: 'sourcetableVisible', render: (value) => (value ? '对外展示' : '隐藏') },
    { title: '最近上报', dataIndex: 'lastSeenAt', render: formatDateTime },
  ];

  const mobileColumns: ColumnsType<MobileStationRow> = [
    {
      title: '移动站',
      dataIndex: 'accessAccount',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/access-monitor/mobile-stations/${row.id}`}>{row.accessAccount}</Link>
          <span>{row.displayName}</span>
        </div>
      ),
    },
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '归属账号', dataIndex: 'ownerAccount' },
    { title: '挂载点', dataIndex: 'mountPoint' },
    { title: '客户端 IP', dataIndex: 'clientIp' },
    { title: '本次时长', dataIndex: 'durationSeconds', render: humanDuration },
    { title: '本次流量', dataIndex: 'trafficMb', render: (value) => `${value.toFixed(2)} MB` },
    { title: '下行', dataIndex: 'sendBps', render: humanBps },
    { title: '上行', dataIndex: 'recvBps', render: humanBps },
    { title: '所在节点', dataIndex: 'runtimeName', render: (_, row) => <Link to={`/admin/control/runtimes/${row.runtimeId}`}>{row.runtimeName}</Link> },
    { title: '最近活动', dataIndex: 'lastSeenAt', render: formatDateTime },
  ];

  const rows = mode === 'base' ? visibleBaseRows : visibleMobileRows;
  const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
  const currentPage = Math.min(page, pageCount);
  const pagedRows = rows.slice((currentPage - 1) * pageSize, currentPage * pageSize);

  return (
    <div className="monitor-page">
      <section className="control-table-toolbar">
        <Input.Search
          allowClear
          className="control-search"
          placeholder={mode === 'base' ? '搜索基准站、挂载点、源站账号' : '搜索移动站、归属账号、挂载点'}
          value={search}
          onChange={(event) => {
            setSearch(event.target.value);
            setPage(1);
          }}
        />
        <Button onClick={() => { setSearch(''); setPage(1); }}>重置</Button>
        <Space className="control-table-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="接入监控数据不可用" description={error} /> : null}

      <section className="control-table-frame">
        <Table<BaseStationRow | MobileStationRow>
          columns={(mode === 'base' ? baseColumns : mobileColumns) as ColumnsType<BaseStationRow | MobileStationRow>}
          dataSource={pagedRows}
          loading={loading}
          rowKey="id"
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="当前没有在线接入记录" /> }}
        />
      </section>
      <ControlPaginationBar
        current={currentPage}
        pageSize={pageSize}
        total={rows.length}
        onChange={(nextPage, nextPageSize) => {
          setPage(nextPage);
          setPageSize(nextPageSize);
        }}
      />
    </div>
  );
}
