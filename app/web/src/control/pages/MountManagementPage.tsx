import { Link } from 'react-router-dom';
import { DeleteOutlined, ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Empty, Input, Popconfirm, Space, Switch, Table, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { RuntimeSummary } from '../../api/contracts';
import { StatusBadge } from '../components/StatusBadge';
import { ControlPaginationBar } from '../components/TablePage';
import { formatDateTime } from './useControlPage';
import { historicalBaseStations, humanBps, type BaseStationRow } from './accessMonitorModel';

const hiddenStorageKey = 'navcaster.hiddenMountAssets';

function readHiddenIds() {
  try {
    return JSON.parse(localStorage.getItem(hiddenStorageKey) || '[]') as string[];
  } catch {
    return [];
  }
}

export default function MountManagementPage() {
  const [runtimes, setRuntimes] = useState<RuntimeSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [search, setSearch] = useState('');
  const [hiddenIds, setHiddenIds] = useState<string[]>(readHiddenIds);
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
      setError(err instanceof Error ? err.message : '挂载点数据加载失败');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  function removeRow(row: BaseStationRow) {
    const next = Array.from(new Set([...hiddenIds, row.id]));
    setHiddenIds(next);
    localStorage.setItem(hiddenStorageKey, JSON.stringify(next));
    message.success('历史基准站已从本地管理视图移除');
  }

  const rows = useMemo(() => {
    const hidden = new Set(hiddenIds);
    const query = search.trim().toLowerCase();
    return historicalBaseStations(runtimes)
      .filter((row) => !hidden.has(row.id))
      .filter((row) => !query || [row.stationName, row.mountPoint, row.groupName, row.sourceAccount, row.runtimeName].some((item) => item.toLowerCase().includes(query)));
  }, [hiddenIds, runtimes, search]);

  const columns: ColumnsType<BaseStationRow> = [
    {
      title: '基准站资产',
      dataIndex: 'stationName',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <strong>{row.stationName}</strong>
          <span>{row.mountPoint}</span>
        </div>
      ),
    },
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '分组', dataIndex: 'groupName' },
    { title: '源站账号', dataIndex: 'sourceAccount' },
    { title: '源列表展示', dataIndex: 'sourcetableVisible', render: (value) => <Switch checked={value} checkedChildren="展示" unCheckedChildren="隐藏" disabled /> },
    { title: '当前客户端', dataIndex: 'clientCount', align: 'right' },
    { title: '源站上行', dataIndex: 'recvBps', render: humanBps },
    { title: '所在节点', dataIndex: 'runtimeName', render: (_, row) => <Link to={`/admin/control/runtimes/${row.runtimeId}`}>{row.runtimeName}</Link> },
    { title: '最近登录', dataIndex: 'connectedAt', render: formatDateTime },
    { title: '最近上报', dataIndex: 'lastSeenAt', render: formatDateTime },
    {
      title: '操作',
      fixed: 'right',
      width: 220,
      render: (_, row) => (
        <Space wrap size={4}>
          <Button size="small">分组</Button>
          <Button size="small">源列表</Button>
          <Popconfirm title="确认从挂载点管理中删除这条历史基准站记录？" onConfirm={() => removeRow(row)}>
            <Button size="small" danger icon={<DeleteOutlined />}>删除</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
  const currentPage = Math.min(page, pageCount);
  const pagedRows = rows.slice((currentPage - 1) * pageSize, currentPage * pageSize);

  return (
    <div className="mount-management-page">
      <section className="control-table-toolbar">
        <Input.Search
          allowClear
          className="control-search"
          placeholder="搜索基准站、挂载点、分组或源站账号"
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

      {error ? <Alert className="control-table-alert" type="error" showIcon message="挂载点数据不可用" description={error} /> : null}
      <section className="control-table-frame">
        <Table<BaseStationRow>
          columns={columns}
          dataSource={pagedRows}
          loading={loading}
          rowKey="id"
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="暂无基准站资产记录" /> }}
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
