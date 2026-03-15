# electron-wns

Node native addon that allows an electron/node app to receive push messages from Windows Push Notifications Services (WNS).

- In an early phase of development, but so far I have been able to successfully
use this library to obtain a channel URI (with an included token), that could be used to send WNS
push messages to.
- Tested with Electron 32.2.4, but probably will work with most modern versions of electron (and node)
- Tested by launching in a packaged, and code signed context via a .MSIX installer (see https://www.electronforge.io/config/makers/msix)

- Library is actively being improved!

## USAGE

1. Clone this repo
2. npm install
3. npm run build (Electron version is auto-detected from your app's package.json)
4. Copy the built electron_wns.node into your electron project.
5. Ensure that it is packaged up with the app. For electron packager / forge, you may need to adjust packager config:

Example:
- Place the .node file in assets/win32/wns/electron_wns.node within your repo.
- Adjust packager config to ensure that folder is packaged up with the application files:
```
  packagerConfig: {
    extraResource: [
      `./assets/${process.platform}`
    ]
  }
```
- To reference the .node file by its absolute path I recommend a helper function
that works both when the app is packaged or not:

src/Assets.ts
```
import { app } from 'electron';
import path from 'path';

// Helper functions for accessing assets/ folder when packaged with running electron app.
class Assets {
  static getURL() {
    if (app.isPackaged) {
      return process.resourcesPath;
    } else {
      return path.normalize(`${__dirname}../../../assets`);
    }
  }
}

export default Assets;
```

- require() the .node file to use it in the javascripts of your main process like so:
```
  const assetsURL = Assets.getURL();
  const addonPath = path.join(assetsURL, 'win32', 'wns', 'electron_wns.node');

  const runtimeRequire = typeof __non_webpack_require__ === 'function' ? __non_webpack_require__ : require;

  // Note: its a good idea to try/catch the following require() statement which will throw a MODULE_NOT_FOUND
  // if the absolute path the .node file is wrong, or is compiled/built againts the wrong electron/node ABI.
  electronWNS = runtimeRequire(addonPath); 

  const channel = await electronWNS.getChannel();
  console.log('WNS channel URI:', channel.uri); // Your backend can use this to send to this device

  electronWNS.startForegroundNotifications((notification) => {
    console.log('Foreground WNS notification:', notification);
  });

  electronWNS.stopForegroundNotifications();
```

## Exposed API of electron_wns.node:

### getChannel()
Call this to get a channel URI, that you can then send to your backend, and use to push messages to the user:
```
getChannel(): Promise<{ uri: string; expirationTicks: number }>
```

Returns an object with:
- uri: string usually of the form: https://wns2-bl2p.notify.windows.com/?token=<STRING>
- expirationTicks: integer number of ticks since the epock, indicating when this channel expires.

### startForegroundNotifications()
Starts listening for foreground notifications (notifications while the app is running).
The callback function will be invoked by the library passing the received notification object.

```
startForegroundNotifications(callback: (notification) => void): void
```

The notification object will have the following structure:
```js
{
  type: 'raw' | 'toast' | 'tile' | 'badge' | 'unknown',
  content: string,
  headers: Record<string, string> // populated for raw notifications
}
```

### stopForegroundNotifications()
```
stopForegroundNotifications(): void
```

## TO COME:
- Improving the way the library is integrated into your app - so that you can npm install, and access via a javascript wrapper
- Typescript types/etc.

## An Alternative Library:
- There is the NodeRT project: https://github.com/NodeRT/NodeRT
- But that is currently very outdated and does not compile against more modern versions of node/electron