const wns = require('./index');

(async () => {
  const channel = await wns.getChannel();
  console.log('URI:', channel.uri);
  console.log('Expiration ticks:', channel.expirationTicks);

  wns.startForegroundNotifications((notification) => {
    console.log('Foreground notification:', notification);
  });

  console.log('Listening for foreground WNS notifications. Press Ctrl+C to exit.');
})();
