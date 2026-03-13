# electron-wns

Node native addon for Windows Push Notifications Services (WNS) in Electron.

## Exposed API

- `getChannel(): Promise<{ uri: string; expirationTicks: number }>`
- `startForegroundNotifications(callback: (notification) => void): void`
- `stopForegroundNotifications(): void`

## Notification payload shape

```js
{
  type: 'raw' | 'toast' | 'tile' | 'badge' | 'unknown',
  content: string,
  headers: Record<string, string> // populated for raw notifications
}
```

## Usage

```js
const wns = require('./index');

async function main() {
  const channel = await wns.getChannel();
  console.log('WNS channel URI:', channel.uri);

  wns.startForegroundNotifications((notification) => {
    console.log('Foreground WNS notification:', notification);
  });
}

main().catch(console.error);
```

## Build

```powershell
npm install
npm run build
```

> Note: WNS channel creation requires the app to have valid Windows app identity and WNS-capable packaging/registration.
