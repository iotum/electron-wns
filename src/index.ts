/**
 * electron-wns
 *
 * A TypeScript library for receiving Windows Push Notification Service (WNS)
 * push messages in the main process of an Electron application.
 *
 * @example
 * ```ts
 * import { WNSClient } from 'electron-wns';
 *
 * const client = new WNSClient();
 *
 * // Obtain the channel URI and share it with your backend.
 * const channel = await client.getChannel();
 * sendToMyServer(channel.uri);
 *
 * // Start receiving push notifications.
 * client.on('notification', (n) => {
 *   console.log('Received push:', n.notificationType, n.payload);
 * });
 * client.startListening();
 * ```
 */

export { WNSClient } from './wns-client';
export type {
  WNSChannel,
  WNSClientOptions,
  WNSClientEvents,
  WNSNotification,
  WNSNotificationType,
} from './types';
