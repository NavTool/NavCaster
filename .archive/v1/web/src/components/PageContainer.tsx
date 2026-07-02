import React from 'react';
import { Breadcrumb, Typography } from 'antd';
import { Link } from 'react-router-dom';

const { Title, Text } = Typography;

interface BreadcrumbItem {
  title: string;
  path?: string;
}

interface PageContainerProps {
  title: string;
  subtitle?: string;
  breadcrumb?: BreadcrumbItem[];
  extra?: React.ReactNode;
  children: React.ReactNode;
}

const PageContainer: React.FC<PageContainerProps> = ({ title, subtitle, breadcrumb, extra, children }) => (
  <div>
    {breadcrumb && breadcrumb.length > 0 && (
      <Breadcrumb
        style={{ marginBottom: 12 }}
        items={breadcrumb.map(item => ({
          title: item.path ? <Link to={item.path}>{item.title}</Link> : item.title,
        }))}
      />
    )}
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: 20 }}>
      <div>
        <Title level={4} style={{ margin: 0 }}>{title}</Title>
        {subtitle && <Text type="secondary" style={{ fontSize: 13 }}>{subtitle}</Text>}
      </div>
      {extra && <div style={{ flexShrink: 0 }}>{extra}</div>}
    </div>
    {children}
  </div>
);

export default PageContainer;
