# electron-wns

A TypeScript library for receiving [Windows Push Notification Service (WNS)](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/push-notifications/wns-overview) push messages in the **main process** of an [Electron](https://www.electronjs.org/) application.

---

## How WNS works

```
┌──────────┐  1. getChannel()   ┌──────────────────────┐
│ Electron │ ──────────────────▶│   WNS (Microsoft)    │
│   App    │ ◀────────────────── │                      │
└──────────┘  channel URI       └──────────────────────┘
     │
     │  2. Send URI to backend
     ▼
┌──────────┐  3. POST /notify   ┌──────────────────────┐
│  Your    │ ──────────────────▶│   WNS (Microsoft)    │
│ Backend  │                    │                      │
└──────────┘                    └──────────┬───────────┘
                                           │ 4. push delivered
                                           ▼
                                    ┌──────────┐
                                    │ Electron │  ◀── 'notification' event
                                    │   App    │
                                    └──────────┘
```

1. Your Electron app calls `client.getChannel()` to obtain a **channel URI** from WNS.
2. Your app sends the channel URI to your backend server.
3. Whenever your backend needs to push a message, it makes an authenticated HTTP POST to the channel URI via WNS.
4. WNS delivers the push to the device; `electron-wns` emits a `notification` event in your Electron main process.

---

## Requirements

| Requirement | Details |
|---|---|
| **Operating system** | Windows 10 (build 1803) or later |
| **PowerShell** | `powershell.exe` (Windows PowerShell 5.1+) or `pwsh` (PowerShell 7+) |
| **Package identity** | The Electron app must be packaged as **MSIX** (or have a sparse-package identity) so that `PushNotificationChannelManager` can register the app with WNS. |
| **Node.js / Electron** | Node.js ≥ 18, Electron ≥ 22 |

> **Note – unpackaged Electron apps:** The underlying WinRT API (`PushNotificationChannelManager.CreatePushNotificationChannelForApplicationAsync`) requires a package identity.  
> For unpackaged Win32 apps, consider using the [Windows App SDK push notification API](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/push-notifications/push-quickstart) or packaging your app with MSIX.

---

## Installation

```bash
npm install electron-wns
```

---

## Usage

### Obtain a channel URI

```ts
import { WNSClient } from 'electron-wns';

const client = new WNSClient();

// Get the WNS channel URI for this app instance.
// Share this URI with your backend – it uses it to push notifications.
const channel = await client.getChannel();
console.log('Channel URI:', channel.uri);
console.log('Expires at:', channel.expiresAt);
```

### Listen for incoming push notifications

```ts
import { WNSClient, WNSNotification } from 'electron-wns';

const client = new WNSClient();

client.on('channelUpdated', (channel) => {
  console.log('New channel URI:', channel.uri);
  // Re-register the new URI with your backend
});

client.on('notification', (n: WNSNotification) => {
  console.log('Push received!');
  console.log('  type   :', n.notificationType); // 'toast' | 'tile' | 'badge' | 'raw'
  console.log('  payload:', n.payload);
  console.log('  at     :', n.timestamp);
});

client.on('error', (err) => {
  console.error('WNS error:', err);
});

// Start the background WNS listener
client.startListening();

// Later, when the app is about to quit:
client.stopListening();
```

### Refresh an expired channel

```ts
// Channel URIs have a limited lifetime (check `channel.expiresAt` for the exact expiry).
// Call refreshChannel() on startup or when the channel is nearing its expiry.
const newChannel = await client.refreshChannel();
```

### Custom PowerShell path

```ts
const client = new WNSClient({ powershellPath: 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' });
```

---

## API

### `new WNSClient(options?)`

| Option | Type | Default | Description |
|---|---|---|---|
| `powershellPath` | `string` | `powershell.exe` (Win) / `pwsh` | Path to the PowerShell executable. |

### Methods

| Method | Returns | Description |
|---|---|---|
| `getChannel()` | `Promise<WNSChannel>` | Obtain (and cache) the WNS channel for this app. |
| `refreshChannel()` | `Promise<WNSChannel>` | Force a fresh channel request and emit `channelUpdated`. |
| `startListening()` | `void` | Spawn the background WNS listener. |
| `stopListening()` | `void` | Kill the background listener. |
| `isListening()` | `boolean` | Whether the listener is currently running. |

### Events

| Event | Payload | Description |
|---|---|---|
| `channelUpdated` | `WNSChannel` | Emitted when a new channel URI is obtained. |
| `notification` | `WNSNotification` | Emitted for each incoming push notification. |
| `error` | `Error` | Non-fatal listener error. |

### Types

```ts
interface WNSChannel {
  uri: string;       // The channel URI to give to your backend
  expiresAt: string; // ISO 8601 expiry timestamp – check this field for the exact expiry
}

interface WNSNotification {
  notificationType: 'toast' | 'tile' | 'badge' | 'raw';
  payload: string;   // XML for toast/tile/badge; arbitrary string for raw
  timestamp: string; // ISO 8601
}
```

---

## How it works internally

`electron-wns` ships two PowerShell helper scripts (`scripts/`):

| Script | Purpose |
|---|---|
| `get-channel.ps1` | Calls `PushNotificationChannelManager.CreatePushNotificationChannelForApplicationAsync()` (WinRT) and writes the channel URI as JSON to stdout. |
| `wns-listener.ps1` | Obtains the channel, subscribes to `PushNotificationChannel.PushNotificationReceived`, and continuously writes incoming notification events as JSON to stdout. |

The TypeScript library spawns these scripts as child processes and communicates via newline-delimited JSON on stdout, keeping the library dependency-free at runtime.

---

## License

MIT

