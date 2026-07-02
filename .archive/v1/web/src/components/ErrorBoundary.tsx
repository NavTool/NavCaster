import React from 'react';
import { Result, Button } from 'antd';

interface Props {
  children: React.ReactNode;
}

interface State {
  hasError: boolean;
  message?: string;
}

/**
 * App-level error boundary used as a safety net: any uncaught React render
 * error is caught here and the user is presented with a recovery action that
 * returns them to the login page (clearing any stale auth state).
 */
class ErrorBoundary extends React.Component<Props, State> {
  state: State = { hasError: false };

  static getDerivedStateFromError(err: Error): State {
    return { hasError: true, message: err?.message || String(err) };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    // eslint-disable-next-line no-console
    console.error('[ErrorBoundary]', error, info);
  }

  private handleBackToLogin = () => {
    try {
      localStorage.removeItem('token');
      localStorage.removeItem('authUser');
    } catch { /* ignore */ }
    window.location.hash = '#/login';
    // Force a clean reload so any in-memory state is discarded.
    window.location.reload();
  };

  render() {
    if (!this.state.hasError) return this.props.children;
    return (
      <Result
        status="warning"
        title="页面加载失败"
        subTitle={this.state.message || '无法获取后端数据，请重新登录或检查服务器连接。'}
        extra={
          <Button type="primary" onClick={this.handleBackToLogin}>
            返回登录页
          </Button>
        }
        style={{ marginTop: 80 }}
      />
    );
  }
}

export default ErrorBoundary;
