import { mkdtemp, rm } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawn } from 'node:child_process';

const webBaseUrl = env('WEB_BASE_URL', 'http://127.0.0.1:5173');
const apiHost = env('NAVCASTER_API_HOST', '127.0.0.1');
const apiPort = env('NAVCASTER_API_PORT', '8080');
const debugPort = Number(env('BROWSER_DEBUG_PORT', '9222'));
const headless = env('BROWSER_HEADLESS', 'true') !== 'false';
const chromePath = process.env.CHROME_PATH || process.env.BROWSER_PATH || findChromePath();
const credentials = {
  admin: {
    username: requiredEnv('NAVCASTER_WEB_ADMIN_USER'),
    password: requiredEnv('NAVCASTER_WEB_ADMIN_PASSWORD'),
  },
  user: {
    username: requiredEnv('NAVCASTER_WEB_USER'),
    password: requiredEnv('NAVCASTER_WEB_USER_PASSWORD'),
  },
  supplier: {
    username: requiredEnv('NAVCASTER_WEB_SUPPLIER'),
    password: requiredEnv('NAVCASTER_WEB_SUPPLIER_PASSWORD'),
  },
};

if (!chromePath) {
  throw new Error('Cannot find Chrome or Edge. Set CHROME_PATH or BROWSER_PATH to a Chromium executable.');
}

const browserLogs = [];

async function main() {
  const userDataDir = await mkdtemp(join(tmpdir(), 'navcaster-web-smoke-'));
  const browser = spawn(chromePath, [
    `--remote-debugging-port=${debugPort}`,
    `--user-data-dir=${userDataDir}`,
    '--disable-background-networking',
    '--disable-default-apps',
    '--disable-dev-shm-usage',
    '--disable-extensions',
    '--disable-features=Translate,OptimizationHints',
    '--disable-popup-blocking',
    '--disable-sync',
    '--no-first-run',
    '--no-default-browser-check',
    '--window-size=1440,960',
    ...(headless ? ['--headless=new'] : []),
    'about:blank',
  ], { stdio: ['ignore', 'pipe', 'pipe'] });
  browser.stdout.on('data', (chunk) => pushBrowserLog(chunk));
  browser.stderr.on('data', (chunk) => pushBrowserLog(chunk));

  try {
    await waitForDebugEndpoint(debugPort);
    const page = await CdpPage.create(debugPort);
    await page.enable();

    await assertLoginFlow(page, 'admin', '/admin/dashboard', [
      '运营总览',
      '账号',
      '接入账号',
      '历史站点',
    ]);
    await assertPage(page, '/admin/online-connections', ['在线连接']);
    await assertPage(page, '/admin/audit', ['审计日志']);
    await assertPage(page, '/admin/access-accounts', ['接入账号']);
    await assertPage(page, '/me/access-accounts', ['用户接入账号']);
    await assertPage(page, '/supplier/access-accounts', ['基站接入账号']);

    await assertLoginFlow(page, 'user', '/me/dashboard', [
      '用户工作台',
      '接入账号',
      '授权分组',
      credentials.user.username,
    ]);
    await assertPage(page, '/me/access-accounts', ['用户接入账号']);
    await assertPage(page, '/me/data-push', ['数据推送']);
    await assertForbidden(page, '/admin/dashboard');
    await assertForbidden(page, '/supplier/dashboard');

    await assertLoginFlow(page, 'supplier', '/supplier/dashboard', [
      '供应商工作台',
      '接入账号',
      '供应事实',
      credentials.supplier.username,
    ]);
    await assertPage(page, '/supplier/access-accounts', ['基站接入账号']);
    await assertPage(page, '/supplier/supply-usage', ['供应时长']);
    await assertPage(page, '/supplier/earnings', ['供应收益']);
    await assertForbidden(page, '/admin/dashboard');
    await assertForbidden(page, '/me/dashboard');

    console.log('[web-smoke] PASS three-role browser smoke');
  } catch (error) {
    console.error(`[web-smoke FAIL] ${error?.stack || error}`);
    if (browserLogs.length) {
      console.error('[web-smoke] browser log tail:');
      for (const line of browserLogs.slice(-30)) console.error(line);
    }
    process.exitCode = 1;
  } finally {
    await stopProcess(browser);
    await rm(userDataDir, { recursive: true, force: true });
  }
}

async function assertLoginFlow(page, role, expectedPath, expectedTexts) {
  const account = credentials[role];
  console.log(`[web-smoke] login ${role} as ${account.username}`);
  await page.goto(`${webBaseUrl}/#/login`);
  await page.waitForText('NavCaster 运营平台', 15000);
  await page.fillByPlaceholder('地址', apiHost);
  await page.fillByPlaceholder('端口', apiPort);
  await page.fillByPlaceholder('用户名', account.username);
  await page.fillByPlaceholder('密码', account.password);
  await page.submitLoginForm();
  await page.waitForHash(expectedPath, 15000);
  await page.waitForAllTexts(expectedTexts, 20000);
  await assertNoCrash(page, `${role} home`);
}

async function assertPage(page, path, expectedTexts) {
  console.log(`[web-smoke] page ${path}`);
  await page.goto(`${webBaseUrl}/#${path}`);
  await page.waitForHash(path, 15000);
  await page.waitForAllTexts(expectedTexts, 20000);
  await assertNoCrash(page, path);
}

async function assertForbidden(page, path) {
  console.log(`[web-smoke] forbidden ${path}`);
  await page.goto(`${webBaseUrl}/#${path}`);
  await page.waitForText('无权访问', 15000);
  await page.waitForText('当前账号角色不能进入该运营区域。', 15000);
  await assertNoCrash(page, `forbidden ${path}`);
}

async function assertNoCrash(page, context) {
  const text = await page.bodyText();
  const forbiddenCrashTexts = ['页面加载失败', '无法连接到 NavCaster 后端', '登录失败，请检查地址和凭据'];
  for (const marker of forbiddenCrashTexts) {
    if (text.includes(marker)) {
      throw new Error(`${context} rendered failure marker "${marker}"`);
    }
  }
}

class CdpPage {
  constructor(ws) {
    this.ws = ws;
    this.nextId = 1;
    this.pending = new Map();
    this.events = [];
    this.consoleMessages = [];
    this.ws.onmessage = (event) => this.onMessage(event.data);
  }

  static async create(port) {
    const target = await createTarget(port);
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
      ws.onopen = resolve;
      ws.onerror = () => reject(new Error('failed to connect browser CDP websocket'));
    });
    return new CdpPage(ws);
  }

  async enable() {
    await this.send('Page.enable');
    await this.send('Runtime.enable');
    await this.send('DOM.enable');
  }

  async goto(url) {
    await this.send('Page.navigate', { url });
    await this.waitFor(() => this.eval('document.readyState').then((state) => state === 'complete'), 15000, `load ${url}`);
  }

  async fillByPlaceholder(placeholder, value) {
    const selector = `input[placeholder="${cssEscape(placeholder)}"]`;
    await this.waitForSelector(selector, 10000);
    await this.eval(`
      (() => {
        const el = document.querySelector(${JSON.stringify(selector)});
        el.focus();
        const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
        setter.call(el, ${JSON.stringify(String(value))});
        el.dispatchEvent(new Event('input', { bubbles: true }));
        el.dispatchEvent(new Event('change', { bubbles: true }));
      })()
    `);
  }

  async clickText(text) {
    await this.waitForText(text, 10000);
    const clicked = await this.eval(`
      (() => {
        const targetText = ${JSON.stringify(text)};
        const elements = Array.from(document.querySelectorAll('button, a, [role="button"], .ant-segmented-item'));
        const el = elements.find((item) => (item.innerText || item.textContent || '').trim().includes(targetText));
        if (!el) return false;
        el.scrollIntoView({ block: 'center', inline: 'center' });
        el.click();
        return true;
      })()
    `);
    if (!clicked) throw new Error(`click target not found: ${text}`);
  }

  async submitLoginForm() {
    const clicked = await this.eval(`
      (() => {
        const elements = Array.from(document.querySelectorAll('button'));
        const el = elements.find((item) => (item.innerText || item.textContent || '').replace(/\\s+/g, '').includes('登录'));
        if (!el) return false;
        el.scrollIntoView({ block: 'center', inline: 'center' });
        el.click();
        return true;
      })()
    `);
    if (!clicked) {
      await this.eval(`
        (() => {
          const form = document.querySelector('form');
          if (!form) return false;
          form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
          return true;
        })()
      `);
    }
  }

  async waitForHash(expectedPath, timeoutMs) {
    await this.waitFor(async () => {
      const hash = await this.eval('window.location.hash');
      return hash === `#${expectedPath}`;
    }, timeoutMs, `hash ${expectedPath}`);
  }

  async waitForText(text, timeoutMs) {
    await this.waitFor(async () => {
      const body = await this.bodyText();
      return body.includes(text);
    }, timeoutMs, `text "${text}"`);
  }

  async waitForAllTexts(texts, timeoutMs) {
    for (const text of texts) {
      await this.waitForText(text, timeoutMs);
    }
  }

  async waitForSelector(selector, timeoutMs) {
    await this.waitFor(async () => this.eval(`!!document.querySelector(${JSON.stringify(selector)})`), timeoutMs, `selector ${selector}`);
  }

  async bodyText() {
    return await this.eval('document.body ? document.body.innerText : ""');
  }

  async eval(expression) {
    const result = await this.send('Runtime.evaluate', {
      expression,
      awaitPromise: true,
      returnByValue: true,
      userGesture: true,
    });
    if (result.exceptionDetails) {
      throw new Error(result.exceptionDetails.text || 'Runtime.evaluate failed');
    }
    return result.result?.value;
  }

  async waitFor(predicate, timeoutMs, context) {
    const deadline = Date.now() + timeoutMs;
    let lastError;
    while (Date.now() < deadline) {
      try {
        if (await predicate()) return;
      } catch (error) {
        lastError = error;
      }
      await delay(200);
    }
    const body = await this.bodyText().catch(() => '<body unavailable>');
    const suffix = lastError ? ` last_error=${lastError.message}` : '';
    throw new Error(`timed out waiting for ${context}.${suffix} body=${body.slice(0, 1000)}`);
  }

  send(method, params = {}) {
    const id = this.nextId++;
    const payload = { id, method, params };
    this.ws.send(JSON.stringify(payload));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`CDP command timed out: ${method}`));
      }, 15000);
      this.pending.set(id, { resolve, reject, timer, method });
    });
  }

  onMessage(data) {
    const message = JSON.parse(data);
    if (message.id) {
      const pending = this.pending.get(message.id);
      if (!pending) return;
      clearTimeout(pending.timer);
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(`${pending.method}: ${message.error.message}`));
      else pending.resolve(message.result || {});
      return;
    }
    this.events.push(message);
    if (message.method === 'Runtime.consoleAPICalled') {
      this.consoleMessages.push(message.params);
    }
    if (message.method === 'Runtime.exceptionThrown') {
      const details = message.params?.exceptionDetails;
      const text = details?.exception?.description || details?.text || 'runtime exception';
      console.error(`[web-smoke browser exception] ${text}`);
    }
  }
}

async function createTarget(port) {
  const response = await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' });
  if (!response.ok) {
    throw new Error(`failed to create browser target: HTTP ${response.status}`);
  }
  return await response.json();
}

async function waitForDebugEndpoint(port) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/version`);
      if (response.ok) return;
    } catch {
      // keep waiting
    }
    await delay(250);
  }
  throw new Error(`browser remote debugging endpoint did not open on port ${port}`);
}

function findChromePath() {
  const candidates = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
    'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
  ];
  return candidates.find((candidate) => existsSync(candidate)) || '';
}

function env(name, fallback) {
  return process.env[name] || fallback;
}

function requiredEnv(name) {
  const value = process.env[name];
  if (!value) throw new Error(`missing required environment variable: ${name}`);
  return value;
}

function cssEscape(value) {
  return String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function stopProcess(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) {
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, 5000);
    child.once('exit', () => {
      clearTimeout(timer);
      resolve();
    });
    child.once('error', () => {
      clearTimeout(timer);
      resolve();
    });
    child.kill();
  });
}

function pushBrowserLog(chunk) {
  const lines = chunk.toString().split(/\r?\n/).filter(Boolean);
  browserLogs.push(...lines);
  if (browserLogs.length > 200) {
    browserLogs.splice(0, browserLogs.length - 200);
  }
}

await main();
