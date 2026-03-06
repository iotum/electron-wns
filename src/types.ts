/**
 * Type definitions for electron-wns
 */

/** The type of a WNS notification payload. */
export type WNSNotificationType = 'toast' | 'tile' | 'badge' | 'raw';

/** A notification received from WNS. */
export interface WNSNotification {
  /** The notification type as reported by WNS. */
  notificationType: WNSNotificationType;
  /** The raw string payload of the notification (XML for toast/tile/badge, any string for raw). */
  payload: string;
  /** ISO 8601 timestamp of when the notification was received. */
  timestamp: string;
}

/** A WNS push notification channel. */
export interface WNSChannel {
  /** The channel URI to send to your backend server. */
  uri: string;
  /** ISO 8601 expiry time – request a new channel URI after this point. */
  expiresAt: string;
}

/** Options for creating a {@link WNSClient}. */
export interface WNSClientOptions {
  /**
   * Override the path to `powershell.exe` (or `pwsh`).
   * Defaults to `powershell.exe` on Windows, `pwsh` elsewhere.
   */
  powershellPath?: string;
}

/** Events emitted by {@link WNSClient}. */
export interface WNSClientEvents {
  /** Fired when a new channel URI is obtained from WNS. */
  channelUpdated: [channel: WNSChannel];
  /** Fired each time a push notification arrives. */
  notification: [notification: WNSNotification];
  /** Fired when a recoverable error occurs in the background listener. */
  error: [error: Error];
}
