// Typed MCP WebSocket client for the ESP32 MCP server, with BLE helpers
// (issue #14).  Works in browsers and in Node (pass `WebSocketImpl: WebSocket`
// from the `ws` package to send the token as an Authorization header).
//
// See ble_example.ts for a scan -> connect -> enumerate -> subscribe walk-through.

// WebSocket close codes the server uses to refuse a connection (issue #12).
export const WS_CLOSE_UNAUTHORIZED = 4401;
export const WS_CLOSE_TOO_MANY_CLIENTS = 4429;
const WS_OPEN = 1;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface MCPClientOptions {
    /** MCP access token (serial console: `token show`). */
    token?: string;
    /** WebSocket class that accepts `{ headers }` (Node `ws`). */
    WebSocketImpl?: any;
    /** Default per-request timeout in ms (default 15000). */
    timeoutMs?: number;
}

/** A JSON-RPC error, with the server's structured `code` and `data`. */
export class MCPError extends Error {
    constructor(message: string, public code: number, public data?: any) {
        super(message);
        this.name = 'MCPError';
    }
}

export type BleAddrType = 'public' | 'random' | 'rpa-public' | 'rpa-random';

export interface BleScanOptions {
    /** Scan length; 0 = continuous until bleStopScan(). Default 5000. */
    durationMs?: number;
    /** Capture filters: decide which devices are stored. */
    minRssi?: number;
    manufacturerId?: string;
    serviceUuid?: string;
    macPrefix?: string;
}

export interface BleDevice {
    mac: string;
    name: string;
    rssi: number;
    services: string;
    manufacturer: string;
    connectable: boolean;
    addrType: BleAddrType;
    /** Distinct payloads retained (payloadHistory may show fewer). */
    count: number;
    firstSeen?: number;
    lastSeen?: number;
    payloadHistory: string[];
}

export interface BleScanResults {
    devices: BleDevice[];
    /** Devices dropped this scan to stay within the memory plan's limits. */
    evicted: number;
    maxDevices: number;
    durationMs?: number;
    scanStartedMs?: number;
}

export interface BlePayload {
    payload: string;
    count: number;
    firstSeen?: number;
    lastSeen?: number;
}

export interface BlePayloadRegistry {
    payloads: BlePayload[];
    /** Distinct payloads discarded because the registry was full. */
    dropped: number;
    scanning?: boolean;
    durationMs?: number;
    scanStartedMs?: number;
}

export interface BleCharacteristic {
    handle: number;
    uuid: string;
    /** Comma-separated: read,write,write-no-response,notify,indicate,... */
    properties: string;
    valueHex?: string;
    valueText?: string;
}

export interface BleService {
    uuid: string;
    characteristics: BleCharacteristic[];
}

/** A characteristic by handle (preferred) or by UUID if unique. */
export type BleCharTarget = { handle: number } | { uuid: string };

export interface BleNotification {
    handle: number;
    uuid: string;
    valueHex: string;
    valueText: string;
    ts: number;
    dropped?: number;
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

export class MCPClient {
    private url: string;
    private token: string;
    private WebSocketImpl: any;
    private timeoutMs: number;
    private ws: any = null;
    private requestId = 1;
    private callbacks = new Map<number, { resolve: (m: any) => void; reject: (e: Error) => void }>();
    private subscriptions = new Map<string, (data: any) => void>();
    private bleListeners = new Map<number, (n: BleNotification) => void>();
    private heartbeat: any = null;

    constructor(url: string, options: MCPClientOptions = {}) {
        this.url = url;
        this.token = options.token || '';
        this.WebSocketImpl = options.WebSocketImpl || null;
        this.timeoutMs = options.timeoutMs || 15000;
    }

    connect(): Promise<any> {
        return new Promise((resolve, reject) => {
            this.ws = this.openSocket();
            this.ws.onopen = () => {
                this.startHeartbeat();
                this.initialize().then(resolve).catch(reject);
            };
            this.ws.onmessage = (event: any) => {
                try {
                    this.handleMessage(JSON.parse(String(event.data)));
                } catch (error) {
                    console.error('Error parsing message:', error);
                }
            };
            this.ws.onclose = (event: any) => {
                this.stopHeartbeat();
                for (const { reject: rj } of this.callbacks.values()) rj(new Error('Connection closed'));
                this.callbacks.clear();
                if (event && event.code === WS_CLOSE_UNAUTHORIZED) {
                    reject(new Error('MCP server rejected the access token (4401)'));
                } else if (event && event.code === WS_CLOSE_TOO_MANY_CLIENTS) {
                    reject(new Error('MCP server is at its client limit (4429)'));
                }
            };
            this.ws.onerror = (error: any) => reject(error);
        });
    }

    private openSocket(): any {
        if (this.WebSocketImpl) {
            const headers = this.token ? { Authorization: `Bearer ${this.token}` } : {};
            return new this.WebSocketImpl(this.url, { headers });
        }
        if (!this.token) return new WebSocket(this.url);
        const sep = this.url.includes('?') ? '&' : '?';
        return new WebSocket(`${this.url}${sep}token=${encodeURIComponent(this.token)}`);
    }

    disconnect(): void {
        this.stopHeartbeat();
        if (this.ws) this.ws.close();
    }

    // --- JSON-RPC -------------------------------------------------------------

    /** Send a request; resolves with `result`, rejects with MCPError. */
    request<T = any>(method: string, params: object = {}, timeoutMs = this.timeoutMs): Promise<T> {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WS_OPEN) {
                reject(new Error('WebSocket is not connected'));
                return;
            }
            const id = this.requestId++;
            const timer = setTimeout(() => {
                if (this.callbacks.delete(id)) reject(new Error(`${method}: request timeout`));
            }, timeoutMs);
            this.callbacks.set(id, {
                resolve: (msg) => { clearTimeout(timer); resolve(msg.result as T); },
                reject: (err) => { clearTimeout(timer); reject(err); },
            });
            this.ws.send(JSON.stringify({ jsonrpc: '2.0', method, params, id }));
        });
    }

    private handleMessage(message: any): void {
        if (message.method && message.id === undefined) {
            this.handleNotification(message);
            return;
        }
        const cb = this.callbacks.get(message.id);
        if (!cb) return;
        this.callbacks.delete(message.id);
        if (message.error) {
            cb.reject(new MCPError(message.error.message, message.error.code, message.error.data));
        } else {
            cb.resolve(message);
        }
    }

    private handleNotification(message: any): void {
        if (message.method === 'notifications/resources/updated') {
            const cb = this.subscriptions.get(message.params.uri);
            if (cb) this.readResource(message.params.uri).then(cb);
        } else if (message.method === 'notifications/ble/value') {
            const cb = this.bleListeners.get(message.params.handle);
            if (cb) cb(message.params as BleNotification);
        }
    }

    private startHeartbeat(): void {
        this.heartbeat = setInterval(() => {
            this.request('ping', {}, 5000).catch(() => { /* server heartbeat handles dead links */ });
        }, 30000);
    }

    private stopHeartbeat(): void {
        if (this.heartbeat) clearInterval(this.heartbeat);
        this.heartbeat = null;
    }

    // --- MCP basics -------------------------------------------------------------

    initialize(): Promise<any> {
        return this.request('initialize', {
            protocolVersion: '2025-06-18',
            capabilities: {},
            clientInfo: { name: 'esp32-mcp-client.ts', version: '1.0.0' },
        });
    }

    async listResources(): Promise<any[]> {
        return (await this.request('resources/list')).resources;
    }

    async readResource(uri: string): Promise<any> {
        return this.request('resources/read', { uri });
    }

    async subscribe(uri: string, callback: (data: any) => void): Promise<void> {
        await this.request('resources/subscribe', { uri });
        this.subscriptions.set(uri, callback);
    }

    async unsubscribe(uri: string): Promise<void> {
        await this.request('resources/unsubscribe', { uri });
        this.subscriptions.delete(uri);
    }

    async listTools(): Promise<any[]> {
        return (await this.request('tools/list')).tools;
    }

    callTool(name: string, args: object = {}): Promise<any> {
        return this.request('tools/call', { name, arguments: args });
    }

    // --- BLE (ESP32-S3 builds) ----------------------------------------------------

    /** Start a background scan (fails if one is already running). */
    bleScan(options: BleScanOptions = {}): Promise<{ started: boolean; scanning: boolean }> {
        return this.request('ble/scan', options);
    }

    /** Start a timed scan, wait for it to finish, and return the devices. */
    async bleScanAndCollect(options: BleScanOptions = {}): Promise<BleScanResults> {
        const durationMs = options.durationMs ?? 5000;
        if (durationMs === 0) throw new Error('bleScanAndCollect needs a finite durationMs');
        await this.bleScan({ ...options, durationMs });
        await sleep(durationMs + 200);
        return this.bleDevices();
    }

    /** Stop the scan (if running) and return the captured devices. */
    bleDevices(filter: { mac?: string; serviceUuid?: string; manufacturerId?: string;
                         macPrefix?: string } = {}): Promise<BleScanResults> {
        return this.request('ble/scan/results', filter);
    }

    /** Stop the scan and return the MAC-independent payload registry. */
    blePayloads(): Promise<BlePayloadRegistry> {
        return this.request('ble/scan/payloads');
    }

    /** Payload registry WITHOUT stopping the scan. */
    blePoll(): Promise<BlePayloadRegistry> {
        return this.request('ble/scan/poll');
    }

    bleStopScan(): Promise<{ ok: boolean }> {
        return this.request('ble/scan/stop');
    }

    /** Connect and wait until the link is up (throws if it does not come up). */
    async bleConnect(mac: string, options: { addrType?: BleAddrType; timeoutMs?: number } = {}):
        Promise<{ connected: true; addrType: BleAddrType }> {
        const timeoutMs = options.timeoutMs ?? 5000;
        const started = await this.request('ble/connect', { mac, ...options, timeoutMs });
        const deadline = Date.now() + timeoutMs + 1000;
        for (;;) {
            const st = await this.request('ble/connect/results');
            if (st.connected) return { connected: true, addrType: started.addrType };
            if (!st.connecting || Date.now() > deadline) {
                throw new Error(`BLE connect to ${mac} failed (try addrType "random" for phones and most sensors)`);
            }
            await sleep(200);
        }
    }

    /** Enumerate services and characteristics (with handles) of the connected device. */
    async bleEnumerate(options: { notifyCaptureMs?: number } = {}): Promise<BleService[]> {
        await this.request('ble/services/start');
        const capture = options.notifyCaptureMs ?? 3000;
        const res = await this.request('ble/services/results', { notifyCaptureMs: capture },
                                       capture + 12000);
        return res.services as BleService[];
    }

    /** Write hex bytes to a characteristic (full-control token required). */
    bleWrite(target: BleCharTarget, valueHex: string,
             options: { withResponse?: boolean; timeoutMs?: number } = {}):
        Promise<{ ok: true; handle: number; uuid: string; bytes: number }> {
        return this.request('ble/write', { ...target, value: valueHex, ...options },
                            (options.timeoutMs ?? 2000) + 3000);
    }

    /**
     * Enable notifications/indications and receive values via `onValue`
     * (pushed over the WebSocket as notifications/ble/value).
     */
    async bleSubscribe(target: BleCharTarget, onValue: (n: BleNotification) => void,
                       options: { indicate?: boolean } = {}):
        Promise<{ handle: number; uuid: string; mode: 'notify' | 'indicate' }> {
        const res = await this.request('ble/subscribe', { ...target, ...options });
        this.bleListeners.set(res.handle, onValue);
        return res;
    }

    async bleUnsubscribe(target: BleCharTarget): Promise<void> {
        const res = await this.request('ble/unsubscribe', target);
        this.bleListeners.delete(res.handle);
    }

    /** Values queued since the last call (for when pushes were missed). */
    bleNotifications(): Promise<{ values: BleNotification[]; dropped: number;
                                  subscriptions: number[]; connected: boolean }> {
        return this.request('ble/notifications');
    }

    bleDisconnect(): Promise<{ ok: boolean }> {
        return this.request('ble/disconnect');
    }
}
