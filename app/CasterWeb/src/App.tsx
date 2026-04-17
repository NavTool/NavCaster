import { ConfigProvider, theme } from 'antd';
import zhCN from 'antd/locale/zh_CN';
import AppRouter from './router';

function App() {
  return (
    <ConfigProvider
      locale={zhCN}
      theme={{
        algorithm: theme.darkAlgorithm,
        token: {
          // Spark-inspired dark blue-slate palette
          colorPrimary: '#4a8eff',
          colorBgContainer: '#1e2235',
          colorBgElevated: '#252a40',
          colorBgLayout: '#141625',
          colorBorder: '#2e3450',
          colorBorderSecondary: '#262c42',
          colorText: '#e8eaf0',
          colorTextSecondary: '#9da1b8',
          colorTextTertiary: '#6b7194',
          borderRadius: 8,
          fontFamily: "'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif",
        },
        components: {
          Layout: {
            siderBg: '#12142a',
            headerBg: '#1a1e34',
            bodyBg: '#141625',
          },
          Menu: {
            darkItemBg: '#12142a',
            darkItemSelectedBg: '#1e2845',
            darkItemHoverBg: '#1a2040',
            darkItemColor: '#8b90a8',
            darkItemSelectedColor: '#4a8eff',
          },
          Card: {
            colorBgContainer: '#1e2235',
            colorBorderSecondary: '#2e3450',
          },
          Table: {
            colorBgContainer: '#1e2235',
            headerBg: '#252a40',
            headerColor: '#9da1b8',
            rowHoverBg: '#252a40',
            borderColor: '#2e3450',
          },
          Button: {
            primaryShadow: 'none',
          },
          Modal: {
            contentBg: '#1e2235',
            headerBg: '#1e2235',
          },
          Input: {
            colorBgContainer: '#252a40',
            activeBorderColor: '#4a8eff',
          },
          Select: {
            colorBgContainer: '#252a40',
            optionSelectedBg: '#1e2845',
          },
          Tag: {
            defaultBg: '#252a40',
          },
          Descriptions: {
            colorSplit: '#2e3450',
          },
          Statistic: {
            colorTextDescription: '#8b90a8',
          },
        },
      }}
    >
      <AppRouter />
    </ConfigProvider>
  );
}

export default App;
