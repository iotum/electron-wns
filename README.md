# electron-wns

Simple light-weight library for using Windows Push Notifications Services (WNS) in your electron app.

This npm package exports an index.js file which simply wraps using a node native extension (electron_wns.node), that is built automatically on npm installing this library.

The .node file, written in C++, uses the Component Object Model (COM) to interface with the Windows.Networking.PushNotifications
Windows Runtime (WinRT) APIs to register your app obtaining a "channel URI" (which includes a device token), that can be used by your backend to push messages to your windows electron app. It also allows you to register a javascript function that is called back
by the library when a push message is received.

Currently tested in Electron 32.2.4, and by using electron-forge to build a code-signed .MSXI installer.

IMPORTANT:

This lib is designed to be used in a packaged context (via an appX package / MSIX installer).
(see https://www.electronforge.io/config/makers/msix)

## USAGE

### Method 1: npm install + use the javascript wrapper
1. In your electron project, npm install --save electron-wns
2. Conditionally require/import electron-wns (so that you only attempt to load it on windows)

```
import os from 'os';

if (os.platform() === 'win32') {
  const runtimeRequire = typeof __non_webpack_require__ === 'function' ? __non_webpack_require__ : require;
  const electronWNS = runtimeRequire('electron-wns').default;
}
```

NOTE: this npm package has a postinstall hook which will build the node addon used internally in this library (electron_wns.nod).
It is designed to build against the version of electron/node you are using. When not run on windows, it will exit avoiding building the .node file.

This is the equivalent of running this command in your project folder after npm installing this lib: 
```
electron-rebuild -f -w electron-wns
```

### Method 2: Manually build and include the node addon
Alernatively you can simply manually build the electron_wns.node file, package it up with your electron app and require() it in the
javascripts of your main process.

1. Clone this repo
2. npm install
3. npx electron-rebuild -f -v 32.2.4  (for Electron 32.2.4, elsewise replace with the version of electron you'll be using)
4. Copy build/release/electron_wns.rb into your project (eg. assets/win32/wns/electron_wns.node)
5. Ensure that it is packaged up with the app. For electron packager / forge, you may need to adjust packager config:

Example:
- Place the .node file in assets/win32/wns/electron_wns.node within your repo.
- Adjust packager config to ensure that folder is packaged up with the application files:
```
  packagerConfig: {
    extraResource: [
      `./assets/${process.platform}` // This will only package up the assets/win32 folder, when building for/on windows
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

6. require() the .node file to use it in the javascripts of your main process like so:
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

## Library API:
This npm package contains a small JS wrapper that just exports the functions of electron_wns.node.
Thus the API to this package, and the electron_wns.node file is the same, and is as follows:

### getChannel()
Call this to get a channel URI, that you can then send to your backend, and use to push messages to the user:
```
electronWNS.getChannel(): Promise<{ uri: string; expirationTicks: number }>
```

Returns an object with:
- uri: string usually of the form: https://wns2-bl2p.notify.windows.com/?token=<STRING>
- expirationTicks: integer number of ticks since the epock, indicating when this channel expires.

### startForegroundNotifications()
Starts listening for foreground notifications (notifications while the app is running).
The callback function will be invoked by the library passing the received notification object.

```
electronWNS.startForegroundNotifications(callback: (notification) => void): void
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
electronWNS.stopForegroundNotifications(): void
```

## Test Sending Push Messages
You can run the powershell script (.ps1) file included in this repo to test sending push messages to your app.

```
powershell.exe .\send_wns_message.ps1 --sid <Secure Package SID> --secret <SECRET> --channel <CHANNEL_URI> --type raw --payload "hello world"
```

## An Alternative Library:
- There is the NodeRT project: https://github.com/NodeRT/NodeRT
- But that is currently very outdated and does not compile against more modern versions of node/electron