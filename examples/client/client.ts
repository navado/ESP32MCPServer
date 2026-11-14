// WebSocket close codes the server uses to refuse a connection (issue #12).
const WS_CLOSE_UNAUTHORIZED = 4401;
const WS_CLOSE_TOO_MANY_CLIENTS = 4429;

class MCPClient {
    // options.token          MCP access token (serial console: `token show`).
    // options.WebSocketImpl  a WebSocket class that accepts request headers,
    //                        e.g. require('ws') in Node.  When given, the token
    //                        is sent as `Authorization: Bearer <token>`;
    //                        otherwise (browsers) it goes in `?token=`.
    constructor(url, options = {}) {
        this.url = url;
        this.token = options.token || '';
        this.WebSocketImpl = options.WebSocketImpl || null;
        this.ws = null;
        this.requestId = 1;
        this.callbacks = new Map();
        this.subscriptions = new Map();
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;
        this.heartbeatInterval = null;
    }
    
    connect() {
        return new Promise((resolve, reject) => {
            this.ws = this.openSocket();
            
            this.ws.onopen = () => {
                console.log('Connected to MCP server');
                this.reconnectAttempts = 0;
                this.startHeartbeat();
                this.initialize().then(resolve).catch(reject);
            };
            
            this.ws.onmessage = (event) => {
                try {
                    const message = JSON.parse(event.data);
                    this.handleMessage(message);
                } catch (error) {
                    console.error('Error parsing message:', error);
                }
            };
            
            this.ws.onclose = (event) => {
                console.log('Connection closed', event && event.code);
                this.stopHeartbeat();
                if (event && event.code === WS_CLOSE_UNAUTHORIZED) {
                    // Retrying with the same token cannot succeed.
                    reject(new Error('MCP server rejected the access token (4401)'));
                    return;
                }
                if (event && event.code === WS_CLOSE_TOO_MANY_CLIENTS) {
                    console.log('Server is at its client limit; retrying');
                }
                this.handleReconnect();
            };
            
            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                reject(error);
            };
        });
    }
    
    openSocket() {
        if (this.WebSocketImpl) {
            const headers = this.token ? { Authorization: `Bearer ${this.token}` } : {};
            return new this.WebSocketImpl(this.url, { headers });
        }
        if (!this.token) return new WebSocket(this.url);
        const sep = this.url.includes('?') ? '&' : '?';
        return new WebSocket(`${this.url}${sep}token=${encodeURIComponent(this.token)}`);
    }

    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.stopHeartbeat();
        }
    }
    
    async initialize() {
        const response = await this.sendRequest('initialize', {});
        console.log('Server initialized:', response);
        return response;
    }
    
    async listResources() {
        const response = await this.sendRequest('resources/list', {});
        return response.result.data.resources;
    }
    
    async readResource(uri) {
        const response = await this.sendRequest('resources/read', { uri });
        return response.result.data;
    }
    
    async subscribe(uri, callback) {
        const response = await this.sendRequest('resources/subscribe', { uri });
        if (response.result.success) {
            this.subscriptions.set(uri, callback);
        }
        return response.result.success;
    }
    
    async unsubscribe(uri) {
        const response = await this.sendRequest('resources/unsubscribe', { uri });
        if (response.result.success) {
            this.subscriptions.delete(uri);
        }
        return response.result.success;
    }
    
    sendRequest(method, params) {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                reject(new Error('WebSocket is not connected'));
                return;
            }
            
            const id = this.requestId++;
            const request = {
                jsonrpc: '2.0',
                method: method,
                params: params,
                id: id
            };
            
            this.callbacks.set(id, { resolve, reject });
            this.ws.send(JSON.stringify(request));
            
            // Set timeout for request
            setTimeout(() => {
                if (this.callbacks.has(id)) {
                    this.callbacks.delete(id);
                    reject(new Error('Request timeout'));
                }
            }, 5000);
        });
    }
    
    handleMessage(message) {
        // Handle notifications
        if (message.method && message.method.startsWith('notifications/')) {
            this.handleNotification(message);
            return;
        }
        
        // Handle responses
        if (message.id && this.callbacks.has(message.id)) {
            const { resolve, reject } = this.callbacks.get(message.id);
            this.callbacks.delete(message.id);
            
            if (message.error) {
                reject(new Error(message.error.message));
            } else {
                resolve(message);
            }
        }
    }
    
    handleNotification(message) {
        if (message.method === 'notifications/resources/updated') {
            const uri = message.params.uri;
            const callback = this.subscriptions.get(uri);
            if (callback) {
                this.readResource(uri).then(callback);
            }
        }
    }
    
    handleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('Max reconnection attempts reached');
            return;
        }
        
        this.reconnectAttempts++;
        const delay = this.reconnectDelay * Math.pow(2, this.reconnectAttempts - 1);
        
        console.log(`Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})`);
        setTimeout(() => this.connect(), delay);
    }
    
    startHeartbeat() {
        this.heartbeatInterval = setInterval(() => {
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                this.ws.send(JSON.stringify({ type: 'ping' }));
            }
        }, 30000);
    }
    
    stopHeartbeat() {
        if (this.heartbeatInterval) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
        }
    }
}

// Example usage:
async function connectToMCP() {
    // Browser: token goes in ?token=.  Node: pass { WebSocketImpl: require('ws') }
    // to send it as an Authorization header instead.
    const client = new MCPClient('ws://your-esp32-ip:9000', { token: 'your-mcp-token' });
    
    try {
        // Connect and initialize
        await client.connect();
        
        // List available resources
        const resources = await client.listResources();
        console.log('Available resources:', resources);
        
        // Subscribe to system info updates
        await client.subscribe('system://info', (data) => {
            console.log('System info updated:', data);
        });
        
        // Read current network status
        const networkStatus = await client.readResource('system://network');
        console.log('Network status:', networkStatus);
        
    } catch (error) {
        console.error('Error:', error);
    }
}