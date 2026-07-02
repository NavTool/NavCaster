import React, { useState, useMemo } from 'react';
import { Table, Input, Button, Space, Dropdown } from 'antd';
import { SearchOutlined, DownloadOutlined, SettingOutlined } from '@ant-design/icons';
import type { ColumnsType, TableProps } from 'antd/es/table';

interface DataTableProps<T> extends Omit<TableProps<T>, 'columns'> {
  columns: ColumnsType<T>;
  searchFields?: string[];
  searchPlaceholder?: string;
  exportFilename?: string;
}

function DataTable<T extends Record<string, unknown>>({
  columns,
  dataSource,
  searchFields,
  searchPlaceholder = '搜索...',
  exportFilename,
  ...rest
}: DataTableProps<T>) {
  const [search, setSearch] = useState('');
  const [hiddenCols, setHiddenCols] = useState<Set<string>>(new Set());

  const filtered = useMemo(() => {
    if (!search || !searchFields || !dataSource) return dataSource;
    const q = search.toLowerCase();
    return dataSource.filter(row =>
      searchFields.some(field => {
        const val = row[field];
        return val != null && String(val).toLowerCase().includes(q);
      })
    );
  }, [dataSource, search, searchFields]);

  const visibleColumns = useMemo(
    () => columns.filter(c => !hiddenCols.has(String((c as { key?: string }).key || (c as { dataIndex?: string }).dataIndex || ''))),
    [columns, hiddenCols]
  );

  const handleExport = () => {
    if (!filtered || filtered.length === 0) return;
    const keys = columns.map(c => String((c as { dataIndex?: string }).dataIndex || (c as { key?: string }).key || ''));
    const titles = columns.map(c => String((c as { title?: React.ReactNode }).title || ''));
    const escape = (v: unknown): string => {
      if (v == null) return '';
      const s = typeof v === 'object' ? JSON.stringify(v) : String(v);
      // RFC 4180: 始终用双引号包裹，内部双引号转义为两个双引号
      return '"' + s.replace(/"/g, '""') + '"';
    };
    const csv = [
      titles.map(t => escape(t)).join(','),
      ...filtered.map(row => keys.map(k => escape(row[k])).join(',')),
    ].join('\r\n');
    const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${exportFilename || 'export'}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const colMenuItems = columns.map(c => {
    const key = String((c as { key?: string }).key || (c as { dataIndex?: string }).dataIndex || '');
    return {
      key,
      label: (
        <label style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
          <input
            type="checkbox"
            checked={!hiddenCols.has(key)}
            onChange={e => {
              const next = new Set(hiddenCols);
              e.target.checked ? next.delete(key) : next.add(key);
              setHiddenCols(next);
            }}
          />
          {String((c as { title?: React.ReactNode }).title || key)}
        </label>
      ),
    };
  });

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 12 }}>
        {searchFields ? (
          <Input
            prefix={<SearchOutlined style={{ color: '#6b7194' }} />}
            placeholder={searchPlaceholder}
            value={search}
            onChange={e => setSearch(e.target.value)}
            allowClear
            style={{ width: 280 }}
          />
        ) : <div />}
        <Space>
          {exportFilename && (
            <Button icon={<DownloadOutlined />} size="small" onClick={handleExport}>导出</Button>
          )}
          <Dropdown menu={{ items: colMenuItems }} trigger={['click']}>
            <Button icon={<SettingOutlined />} size="small">列</Button>
          </Dropdown>
        </Space>
      </div>
      <Table
        columns={visibleColumns}
        dataSource={filtered}
        size="small"
        pagination={{ pageSize: 15, showSizeChanger: true, showTotal: t => `共 ${t} 条` }}
        scroll={{ x: 'max-content' }}
        {...rest}
      />
    </div>
  );
}

export default DataTable;
