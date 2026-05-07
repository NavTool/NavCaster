/** 格式化字节数 (1024进制) — 与 QML formatBytes 一致 */
export function formatBytes(bytes: number): string {
  if (!bytes || bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['Byte', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const value = bytes / Math.pow(k, i);
  return value.toFixed(3) + ' ' + sizes[i];
}

/** 格式化网络速率 (bytes/s → bps, 1000进制) — 与 QML formatMbps 一致 */
export function formatMbps(bytesPerSecond: number): string {
  if (!bytesPerSecond || bytesPerSecond === 0) return '0 bps';
  const bitsPerSecond = bytesPerSecond * 8;
  const k = 1000;
  const sizes = ['bps', 'Kbps', 'Mbps', 'Gbps', 'Tbps'];
  const i = Math.floor(Math.log(bitsPerSecond) / Math.log(k));
  const value = bitsPerSecond / Math.pow(k, i);
  return value.toFixed(2) + ' ' + sizes[i];
}

/** 格式化在线时长 (UTC 上线时间戳秒 → "Xd HH:MM:SS") — 与 QML formatTime 一致 */
export function formatOnlineTime(onlineUtcSeconds: number): string {
  if (!onlineUtcSeconds) return '-';
  const nowUtc = Math.floor(Date.now() / 1000);
  let seconds = nowUtc - onlineUtcSeconds;
  if (seconds < 0) seconds = 0;

  const day = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;

  const timeStr =
    String(h).padStart(2, '0') + ':' +
    String(m).padStart(2, '0') + ':' +
    String(s).padStart(2, '0');

  return day > 0 ? `${day}d ${timeStr}` : timeStr;
}

/** 格式化已在线秒数 → "Xd HH:MM:SS" */
export function formatDuration(totalSeconds: number): string {
  if (!totalSeconds) return '-';
  const day = Math.floor(totalSeconds / 86400);
  const h = Math.floor((totalSeconds % 86400) / 3600);
  const m = Math.floor((totalSeconds % 3600) / 60);
  const s = Math.floor(totalSeconds % 60);

  const timeStr =
    String(h).padStart(2, '0') + ':' +
    String(m).padStart(2, '0') + ':' +
    String(s).padStart(2, '0');

  return day > 0 ? `${day}d ${timeStr}` : timeStr;
}

/** 格式化延迟 (微秒 → us/ms/s) — 与 QML formatDelay 一致 */
export function formatDelay(us: number): string {
  if (!us || us === 0) return '-';
  const k = 1000;
  const sizes = ['μs', 'ms', 's'];
  const i = Math.floor(Math.log(us) / Math.log(k));
  const value = us / Math.pow(k, i);
  return value.toFixed(1) + ' ' + sizes[i];
}

/** 格式化负载描述 — 与 QML formatUsage 一致 */
export function formatUsage(percent: number): string {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  if (percent <= 10) return '空闲';
  if (percent <= 30) return '轻量';
  if (percent <= 50) return '正常';
  if (percent <= 70) return '中等';
  if (percent <= 85) return '较高';
  if (percent <= 95) return '高负载';
  return '接近饱和';
}

/** CPU 百分比保持进程单核口径，可能超过 100%。 */
export function normalizeCpuPercent(percent: number): number {
  if (!Number.isFinite(percent)) return 0;
  if (percent < 0) return 0;
  return percent;
}

export function formatCpuPercent(percent: number): string {
  return `${normalizeCpuPercent(percent).toFixed(1)}%`;
}

/** UTC 秒 → 本地时间字符串 "YYYY-MM-DD HH:MM:SS" */
export function getLocalTime(utcSeconds: number): string {
  if (!utcSeconds) return '-';
  const ts = utcSeconds > 1e12 ? utcSeconds : utcSeconds * 1000;
  const d = new Date(ts);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

/** 格式化定位质量 */
export function formatQuality(quality: number): { text: string; color: string } {
  const map: Record<number, { text: string; color: string }> = {
    0: { text: '无', color: 'default' },
    1: { text: '单点', color: 'orange' },
    2: { text: 'DGPS', color: 'blue' },
    4: { text: '固定解', color: 'green' },
    5: { text: '浮动解', color: 'cyan' },
  };
  return map[quality] || { text: `${quality}`, color: 'default' };
}

/** 字节速率简洁显示 */
export function formatSpeed(bytesPerSec: number): string {
  return formatBytes(bytesPerSec) + '/s';
}

// ==================== 坐标转换 ====================

/** WGS84 椭球参数 */
const WGS84_A = 6378137.0;           // 长半轴 (m)
const WGS84_F = 1 / 298.257223563;   // 扁率
const WGS84_E2 = 2 * WGS84_F - WGS84_F * WGS84_F; // 第一偏心率平方

export interface GeodeticCoord {
  lat: number;  // 纬度 (度)
  lng: number;  // 经度 (度)
  height: number; // 椭球高 (m)
  valid: boolean;
}

/** ECEF (X,Y,Z) → 大地坐标 (lat, lng, height)，WGS84 椭球 */
export function ecefToGeodetic(x: number, y: number, z: number): GeodeticCoord {
  // 坐标全为 0 时视为无效
  if (x === 0 && y === 0 && z === 0) {
    return { lat: 0, lng: 0, height: 0, valid: false };
  }

  const lng = Math.atan2(y, x);
  const p = Math.sqrt(x * x + y * y);

  // 迭代初值
  let lat = Math.atan2(z, p * (1 - WGS84_E2));
  let height = 0;

  // Bowring 迭代 (3-4 次足够收敛到 mm 级)
  for (let i = 0; i < 5; i++) {
    const sinLat = Math.sin(lat);
    const N = WGS84_A / Math.sqrt(1 - WGS84_E2 * sinLat * sinLat);
    height = p / Math.cos(lat) - N;
    lat = Math.atan2(z, p * (1 - WGS84_E2 * N / (N + height)));
  }

  return {
    lat: (lat * 180) / Math.PI,
    lng: (lng * 180) / Math.PI,
    height,
    valid: true,
  };
}

/** 格式化纬/经度 (十进制度 → 度°分′秒″ + 十进制度) */
export function formatLatLon(deg: number, type: 'lat' | 'lng'): string {
  const abs = Math.abs(deg);
  const d = Math.floor(abs);
  const m = Math.floor((abs - d) * 60);
  const s = ((abs - d - m / 60) * 3600).toFixed(3);

  const hemi =
    type === 'lat'
      ? (deg >= 0 ? 'N' : 'S')
      : (deg >= 0 ? 'E' : 'W');

  return `${d}°${String(m).padStart(2, '0')}′${parseFloat(s).toFixed(3)}″${hemi} (${deg.toFixed(7)}°)`;
}

/** 格式化椭球高 */
export function formatHeight(h: number): string {
  return `${h.toFixed(3)} m`;
}

/** 格式化 ECEF 坐标 */
export function formatEcef(x: number): string {
  return `${x.toFixed(3)} m`;
}
