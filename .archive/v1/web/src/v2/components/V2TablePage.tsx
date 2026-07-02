import type { ReactNode } from 'react';
import { Alert, Button, Empty, Input, Pagination, Select, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import type { V2ControlStatus } from '../api/contracts';

export interface V2TableFilters {
  search: string;
  status: V2ControlStatus | 'all';
  page: number;
  pageSize: number;
}

export function V2TablePage<T extends object>({
  title,
  description,
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
  title: string;
  description?: string;
  actions?: ReactNode;
  filters: V2TableFilters;
  onFiltersChange: (next: V2TableFilters) => void;
  columns: ColumnsType<T>;
  data: T[];
  total: number;
  rowKey: string | ((record: T) => string);
  loading?: boolean;
  error?: string | null;
  extraFilters?: ReactNode;
}) {
  return (
    <div className="v2-table-page">
      <div className="v2-page-heading">
        <div>
          <h1>{title}</h1>
          {description ? <p>{description}</p> : null}
        </div>
        {actions ? <Space wrap>{actions}</Space> : null}
      </div>

      <section className="v2-table-toolbar">
        <Input.Search
          allowClear
          className="v2-search"
          placeholder="Search name, id, host, or region"
          value={filters.search}
          onChange={(event) => onFiltersChange({ ...filters, search: event.target.value, page: 1 })}
        />
        <Select
          className="v2-status-filter"
          value={filters.status}
          onChange={(status) => onFiltersChange({ ...filters, status, page: 1 })}
          options={[
            { label: 'All states', value: 'all' },
            { label: 'Pending', value: 'pending' },
            { label: 'Running', value: 'running' },
            { label: 'Failed', value: 'failed' },
            { label: 'Draining', value: 'draining' },
            { label: 'Offline', value: 'offline' },
          ]}
        />
        {extraFilters}
        <Button onClick={() => onFiltersChange({ search: '', status: 'all', page: 1, pageSize: filters.pageSize })}>
          Reset
        </Button>
      </section>

      {error ? (
        <Alert
          className="v2-table-alert"
          type="error"
          showIcon
          message="AdminService control API unavailable"
          description={error}
        />
      ) : null}

      <section className="v2-table-frame">
        <Table<T>
          columns={columns}
          dataSource={data}
          loading={loading}
          pagination={false}
          rowKey={rowKey}
          size="middle"
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description={error ? 'Control API did not return usable rows' : 'No control-plane rows matched the current filters'} /> }}
        />
      </section>

      <footer className="v2-pagination-bar">
        <span>{total} records</span>
        <Pagination
          current={filters.page}
          pageSize={filters.pageSize}
          total={total}
          showSizeChanger
          pageSizeOptions={[5, 10, 20, 50]}
          onChange={(page, pageSize) => onFiltersChange({ ...filters, page, pageSize })}
        />
      </footer>
    </div>
  );
}
