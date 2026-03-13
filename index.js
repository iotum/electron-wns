const binding = require('bindings')('electron_wns');

module.exports = {
  getChannel: binding.getChannel,
  startForegroundNotifications: binding.startForegroundNotifications,
  stopForegroundNotifications: binding.stopForegroundNotifications,
};
