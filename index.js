const path = require('path');

const addonPath = path.join(__dirname, 'build', 'Release', 'electron_wns.node');
const binding = require(addonPath);

console.log('==> index.js binding.getChannel: ', typeof binding.getChannel);
module.exports = {
  getChannel: binding.getChannel,
  startForegroundNotifications: binding.startForegroundNotifications,
  stopForegroundNotifications: binding.stopForegroundNotifications,
  isWinAppSdkPushSupported: binding.isWinAppSdkPushSupported,
  getWinAppSdkPushDiagnostics: binding.getWinAppSdkPushDiagnostics,
  registerPush: binding.registerPush,
  unregisterPush: binding.unregisterPush,
  getChannelForPushManager: binding.getChannelForPushManager,
};
