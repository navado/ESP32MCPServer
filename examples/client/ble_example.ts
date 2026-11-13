// BLE walk-through over the MCP WebSocket (issue #14):
// scan -> pick a connectable device -> connect -> enumerate -> optionally
// subscribe to a notifiable characteristic for a few seconds.
//
//   npm install ws tsx
//   MCP_URL=ws://192.168.1.50:9000 MCP_TOKEN=<token> npx tsx ble_example.ts [mac]
//
// Without a MAC argument the strongest connectable device from the scan is used.

import WebSocket from 'ws';
import { MCPClient, MCPError } from './client';

async function main() {
    const url = process.env.MCP_URL || 'ws://esp32.local:9000';
    const token = process.env.MCP_TOKEN || '';
    const client = new MCPClient(url, { token, WebSocketImpl: WebSocket });
    await client.connect();

    try {
        console.log('Scanning for 5 s...');
        const scan = await client.bleScanAndCollect({ durationMs: 5000 });
        for (const d of scan.devices) {
            console.log(`  ${d.mac} ${d.addrType.padEnd(10)} ${String(d.rssi).padStart(4)} dBm ` +
                        `${d.connectable ? 'conn' : '    '} ${d.name}`);
        }
        if (scan.evicted) console.log(`  (${scan.evicted} devices evicted to stay within limits)`);

        const mac = process.argv[2] ||
            scan.devices.filter((d) => d.connectable).sort((a, b) => b.rssi - a.rssi)[0]?.mac;
        if (!mac) {
            console.log('No connectable device found.');
            return;
        }

        console.log(`Connecting to ${mac}...`);
        await client.bleConnect(mac, { timeoutMs: 5000 });

        console.log('Enumerating services...');
        const services = await client.bleEnumerate({ notifyCaptureMs: 2000 });
        for (const s of services) {
            console.log(`  service ${s.uuid}`);
            for (const c of s.characteristics) {
                const v = c.valueText || c.valueHex || '';
                console.log(`    [${c.handle}] ${c.uuid} (${c.properties}) ${v}`);
            }
        }

        const notifiable = services.flatMap((s) => s.characteristics)
            .find((c) => /(^|,)(notify|indicate)(,|$)/.test(c.properties));
        if (notifiable) {
            console.log(`Subscribing to handle ${notifiable.handle} for 5 s...`);
            await client.bleSubscribe({ handle: notifiable.handle }, (n) => {
                console.log(`  notify [${n.handle}] ${n.valueHex} ${n.valueText}`);
            });
            await new Promise((r) => setTimeout(r, 5000));
            await client.bleUnsubscribe({ handle: notifiable.handle });
        }
    } catch (e) {
        if (e instanceof MCPError) {
            // Structured errors carry what is blocking and how to resolve it.
            console.error(`MCP error ${e.code}: ${e.message}`, e.data ?? '');
        } else {
            throw e;
        }
    } finally {
        await client.bleDisconnect().catch(() => {});
        client.disconnect();
    }
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
