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
