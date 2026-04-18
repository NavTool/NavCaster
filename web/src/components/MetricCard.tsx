import React from 'react';
import { Card, Statistic } from 'antd';
import { ArrowUpOutlined, ArrowDownOutlined } from '@ant-design/icons';

interface MetricCardProps {
  title: string;
  value: string | number;
  prefix?: React.ReactNode;
  suffix?: string;
  trend?: 'up' | 'down';
  trendValue?: string;
  style?: React.CSSProperties;
}

const MetricCard: React.FC<MetricCardProps> = ({ title, value, prefix, suffix, trend, trendValue, style }) => (
  <Card style={{ borderColor: '#2e3450', ...style }}>
    <Statistic
      title={title}
      value={value}
      prefix={prefix}
      suffix={
        <>
          {suffix && <span style={{ fontSize: 13, color: '#8b90a8' }}>{suffix}</span>}
          {trend && trendValue && (
            <span style={{
              fontSize: 12, marginLeft: 8,
              color: trend === 'up' ? '#52c41a' : '#ff4d4f',
            }}>
              {trend === 'up' ? <ArrowUpOutlined /> : <ArrowDownOutlined />}
              {' '}{trendValue}
            </span>
          )}
        </>
      }
    />
  </Card>
);

export default MetricCard;
