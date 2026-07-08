import type { ReactNode } from 'react';
import { Alert, Button, Empty, Input, Pagination, Select, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import type { ControlStatus } from '../../api/contracts';

export interface TableFilters {
  search: string;
  status: ControlStatus | 'all';
  page: number;
  pageSize: number;
}

const pageSizeOptions = [5, 10, 20, 50];

export function ControlPaginationBar({
  current,
  pageSize,
  total,
  onChange,
}: {
  current: number;
  pageSize: number;
  total: number;
  onChange: (page: number, pageSize: number) => void;
}) {
  return (
    <footer className="control-pagination-bar">
      <span>共 {total} 条记录</span>
      <div className="control-pagination-size">
        <span>每页显示</span>
        <Select
          size="small"
          value={pageSize}
          options={pageSizeOptions.map((value) => ({ label: String(value), value }))}
          onChange={(nextPageSize) => onChange(1, nextPageSize)}
        />
        <span>条</span>
      </div>
      <Pagination
        current={current}
        pageSize={pageSize}
        total={total}
        showSizeChanger={false}
        onChange={(page) => onChange(page, pageSize)}
      />
    </footer>
  );
}

export function TablePage<T extends object>({
  actions,
  filters,
  onFiltersChange,
  columns,
  data,
  total,
  rowKey,
  loading,
  error,
  extraFilters,
}: {
  actions?: ReactNode;
  filters: TableFilters;
  onFiltersChange: (next: TableFilters) => void;
  columns: ColumnsType<T>;
  data: T[];
  total: number;
  rowKey: string | ((record: T) => string);
  loading?: boolean;
  error?: string | null;
  extraFilters?: ReactNode;
}) {
  return (
    <div className="control-table-page">
      <section className="control-table-toolbar">
        <Input.Search
          allowClear
          className="control-search"
          placeholder="搜索名称、ID、主机或区域"
          value={filters.search}
          onChange={(event) => onFiltersChange({ ...filters, search: event.target.value, page: 1 })}
        />
        <Select
          className="control-status-filter"
          value={filters.status}
          onChange={(status) => onFiltersChange({ ...filters, status, page: 1 })}
          options={[
            { label: '全部状态', value: 'all' },
            { label: '待处理', value: 'pending' },
            { label: '运行中', value: 'running' },
            { label: '失败', value: 'failed' },
            { label: '排空中', value: 'draining' },
            { label: '离线', value: 'offline' },
          ]}
        />
        {extraFilters}
        <Button onClick={() => onFiltersChange({ search: '', status: 'all', page: 1, pageSize: filters.pageSize })}>
          重置
        </Button>
        {actions ? <Space className="control-table-toolbar-actions" wrap>{actions}</Space> : null}
      </section>

      {error ? (
        <Alert
          className="control-table-alert"
          type="error"
          showIcon
          message="AdminService 控制接口不可用"
          description={error}
        />
      ) : null}

      <section className="control-table-frame">
        <Table<T>
          columns={columns}
          dataSource={data}
          loading={loading}
          pagination={false}
          rowKey={rowKey}
          size="middle"
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description={error ? '控制接口没有返回可用数据' : '当前筛选条件下没有数据'} /> }}
        />
      </section>

      <ControlPaginationBar
        current={filters.page}
        pageSize={filters.pageSize}
        total={total}
        onChange={(page, pageSize) => onFiltersChange({ ...filters, page, pageSize })}
      />
    </div>
  );
}
