import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { V2Shell } from './v2/components/V2Shell';
import V2ConfigVersionsPage from './v2/pages/V2ConfigVersionsPage';
import V2HostsPage from './v2/pages/V2HostsPage';
import V2RuntimeDetailPage from './v2/pages/V2RuntimeDetailPage';
import V2RuntimesPage from './v2/pages/V2RuntimesPage';
import V2WorkersPage from './v2/pages/V2WorkersPage';

export default function AppRouter() {
  return (
    <HashRouter>
      <Routes>
        <Route path="/" element={<Navigate to="/admin/control/hosts" replace />} />
        <Route path="/v2/*" element={<Navigate to="/admin/control/hosts" replace />} />
        <Route path="/admin/control" element={<V2Shell />}>
          <Route index element={<Navigate to="/admin/control/hosts" replace />} />
          <Route path="hosts" element={<V2HostsPage />} />
          <Route path="runtimes" element={<V2RuntimesPage />} />
          <Route path="runtimes/:id" element={<V2RuntimeDetailPage />} />
          <Route path="workers" element={<V2WorkersPage />} />
          <Route path="config" element={<V2ConfigVersionsPage />} />
        </Route>
        <Route path="*" element={<Navigate to="/admin/control/hosts" replace />} />
      </Routes>
    </HashRouter>
  );
}
