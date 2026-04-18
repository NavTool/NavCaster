import React from 'react';
import { Modal } from 'antd';
import { ExclamationCircleFilled } from '@ant-design/icons';

interface ConfirmActionProps {
  title: string;
  description: string;
  onConfirm: () => void | Promise<void>;
  danger?: boolean;
  children: React.ReactElement;
}

const ConfirmAction: React.FC<ConfirmActionProps> = ({ title, description, onConfirm, danger = true, children }) => {
  const handleClick = () => {
    Modal.confirm({
      title,
      icon: <ExclamationCircleFilled />,
      content: description,
      okText: '确认',
      cancelText: '取消',
      okButtonProps: danger ? { danger: true } : {},
      onOk: onConfirm,
    });
  };

  return React.cloneElement(children, { onClick: handleClick });
};

export default ConfirmAction;
