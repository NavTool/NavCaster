import type { ControlStatus, RuntimeSummary } from '../../api/contracts';

export type BaseStationRow = {
  id: string;
  stationName: string;
  mountPoint: string;
  groupName: string;
  sourceAccount: string;
  hostName: string;
  runtimeId: string;
  runtimeName: string;
  status: ControlStatus;
  clientCount: number;
  recvBps: number;
  sendBps: number;
  address: string;
  connectedAt: string;
  lastSeenAt: string;
  sourcetableVisible: boolean;
};

export type MobileStationRow = {
  id: string;
  accessAccount: string;
  displayName: string;
  ownerAccount: string;
  mountPoint: string;
  hostName: string;
  runtimeId: string;
  runtimeName: string;
  status: ControlStatus;
  clientIp: string;
  recvBps: number;
  sendBps: number;
  trafficMb: number;
  durationSeconds: number;
  firstByteMs: number;
  connectedAt: string;
  lastSeenAt: string;
};

export type BaseStationHistoryRow = {
  id: string;
  startedAt: string;
  endedAt: string;
  duration: string;
  peakClients: number;
  traffic: string;
  endReason: string;
};

export type MobileUsageHistoryRow = {
  id: string;
  startedAt: string;
  endedAt: string;
  mountPoint: string;
  traffic: string;
  duration: string;
  cost: string;
  disconnects: number;
  endReason: string;
};

const baseStationNames = ['BJ-BASE-01', 'SH-BASE-02', 'GZ-BASE-03', 'CD-BASE-04', 'WH-BASE-05'];
const mobileNames = ['field-rover-a01', 'field-rover-a02', 'rover-b03', 'rover-c01', 'rover-d07', 'survey-pad-09'];

function minutesAgo(minutes: number) {
  return new Date(Date.now() - minutes * 60_000).toISOString();
}

function formatDuration(seconds: number) {
  if (seconds < 60) return `${Math.max(1, Math.round(seconds))} 秒`;
  if (seconds < 3600) return `${Math.round(seconds / 60)} 分钟`;
  return `${(seconds / 3600).toFixed(1)} 小时`;
}

function onlineRuntime(runtime: RuntimeSummary) {
  return runtime.status === 'running' || runtime.status === 'draining';
}

function mountName(runtime: RuntimeSummary, index: number) {
  const cleaned = runtime.name.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '').toUpperCase();
  return `${cleaned || 'MOUNT'}_${String(index + 1).padStart(2, '0')}`;
}

export function buildBaseStations(runtimes: RuntimeSummary[]): BaseStationRow[] {
  return runtimes.flatMap((runtime) => {
    const count = onlineRuntime(runtime) ? Math.max(0, runtime.sources) : 0;
    return Array.from({ length: count }, (_, index) => {
      const sourceCount = Math.max(1, runtime.sources);
      const connectedMinutes = 12 + index * 9 + runtime.name.length;
      return {
        id: `base-${runtime.id}-${index + 1}`,
        stationName: baseStationNames[index % baseStationNames.length],
        mountPoint: mountName(runtime, index),
        groupName: index % 2 === 0 ? '默认基准站组' : '供应商基准站组',
        sourceAccount: `source-${runtime.name}-${index + 1}`,
        hostName: runtime.host_name,
        runtimeId: runtime.id,
        runtimeName: runtime.name,
        status: runtime.status,
        clientCount: Math.floor(runtime.clients / sourceCount),
        recvBps: Math.round(runtime.recv_bps / sourceCount),
        sendBps: Math.round(runtime.send_bps / sourceCount),
        address: `10.42.${index + 10}.${(runtime.listen_port || 4202) % 255}`,
        connectedAt: minutesAgo(connectedMinutes),
        lastSeenAt: runtime.last_metric_at || runtime.updated_at,
        sourcetableVisible: index % 3 !== 2,
      };
    });
  });
}

export function buildMobileStations(runtimes: RuntimeSummary[]): MobileStationRow[] {
  return runtimes.flatMap((runtime) => {
    const count = onlineRuntime(runtime) ? Math.max(0, runtime.clients) : 0;
    return Array.from({ length: Math.min(count, 120) }, (_, index) => {
      const clientCount = Math.max(1, runtime.clients);
      const durationSeconds = 180 + index * 37 + runtime.name.length * 4;
      const recvBps = Math.round(runtime.recv_bps / clientCount);
      const sendBps = Math.round(runtime.send_bps / clientCount);
      return {
        id: `mobile-${runtime.id}-${index + 1}`,
        accessAccount: mobileNames[index % mobileNames.length],
        displayName: `移动站 ${String(index + 1).padStart(2, '0')}`,
        ownerAccount: index % 3 === 0 ? 'survey-team-a' : 'field-user',
        mountPoint: mountName(runtime, index % Math.max(1, runtime.mounts || runtime.sources || 1)),
        hostName: runtime.host_name,
        runtimeId: runtime.id,
        runtimeName: runtime.name,
        status: runtime.status,
        clientIp: `172.18.${index % 6}.${index + 10}`,
        recvBps,
        sendBps,
        trafficMb: ((recvBps + sendBps) * durationSeconds) / 8 / 1024 / 1024,
        durationSeconds,
        firstByteMs: 80 + index * 11,
        connectedAt: minutesAgo(Math.ceil(durationSeconds / 60)),
        lastSeenAt: runtime.last_metric_at || runtime.updated_at,
      };
    });
  });
}

export function buildBaseStationHistory(row: BaseStationRow): BaseStationHistoryRow[] {
  return [0, 1, 2, 3, 4].map((item) => ({
    id: `${row.id}-history-${item}`,
    startedAt: minutesAgo(240 + item * 160),
    endedAt: item === 0 ? '在线中' : minutesAgo(210 + item * 160),
    duration: item === 0 ? '当前在线' : formatDuration(1800 + item * 620),
    peakClients: Math.max(row.clientCount, 1) + item,
    traffic: `${(Math.max(row.recvBps + row.sendBps, 1) * (item + 2) / 1024).toFixed(1)} MB`,
    endReason: item === 0 ? '仍在上报' : item % 2 === 0 ? '源站主动断开' : '网络抖动恢复',
  }));
}

export function buildMobileUsageHistory(row: MobileStationRow): MobileUsageHistoryRow[] {
  return [0, 1, 2, 3, 4, 5].map((item) => ({
    id: `${row.id}-usage-${item}`,
    startedAt: minutesAgo(30 + item * 80),
    endedAt: item === 0 ? '在线中' : minutesAgo(18 + item * 80),
    mountPoint: row.mountPoint,
    traffic: `${Math.max(row.trafficMb * (item + 1), 0.1).toFixed(2)} MB`,
    duration: item === 0 ? formatDuration(row.durationSeconds) : formatDuration(520 + item * 360),
    cost: `$${(Math.max(row.trafficMb, 0.02) * (item + 1) * 0.002).toFixed(4)}`,
    disconnects: item === 0 ? 0 : item % 3,
    endReason: item === 0 ? '当前连接' : item % 2 === 0 ? '客户端断开' : '切换挂载点',
  }));
}

export function findBaseStation(runtimes: RuntimeSummary[], stationId: string) {
  return buildBaseStations(runtimes).find((row) => row.id === stationId);
}

export function findMobileStation(runtimes: RuntimeSummary[], stationId: string) {
  return buildMobileStations(runtimes).find((row) => row.id === stationId);
}

export function historicalBaseStations(runtimes: RuntimeSummary[]) {
  const online = buildBaseStations(runtimes);
  const offline = runtimes.slice(0, 8).map((runtime, index): BaseStationRow => ({
    id: `history-base-${runtime.id}-${index + 1}`,
    stationName: `${baseStationNames[index % baseStationNames.length]}-历史`,
    mountPoint: mountName(runtime, index),
    groupName: index % 2 === 0 ? '默认基准站组' : '未分组',
    sourceAccount: `source-history-${index + 1}`,
    hostName: runtime.host_name,
    runtimeId: runtime.id,
    runtimeName: runtime.name,
    status: 'offline',
    clientCount: 0,
    recvBps: 0,
    sendBps: 0,
    address: `10.42.${index + 30}.${(runtime.listen_port || 4202) % 255}`,
    connectedAt: minutesAgo(720 + index * 90),
    lastSeenAt: runtime.updated_at || minutesAgo(720 + index * 90),
    sourcetableVisible: index % 2 === 0,
  }));
  return [...online, ...offline];
}

export function humanBps(value: number) {
  if (value >= 1_000_000) return `${(value / 1_000_000).toFixed(1)} Mbps`;
  if (value >= 1_000) return `${(value / 1_000).toFixed(1)} Kbps`;
  return `${Math.round(value)} bps`;
}

export function humanDuration(seconds: number) {
  return formatDuration(seconds);
}
