/**
 * The type of a received WNS push notification.
 */
export type WnsNotificationType = 'toast' | 'tile' | 'badge' | 'raw' | 'unknown';

/**
 * A WNS push notification received while the app is in the foreground.
 */
export interface WnsNotification {
  /** The notification type. */
  type: WnsNotificationType;
  /**
   * The notification payload. For toast notifications this is the XML content
   * of the notification; for raw notifications this is the raw string body.
   */
  content: string;
  /**
   * HTTP headers associated with the notification.
   * Populated for raw notifications; empty for all other types.
   */
  headers: Record<string, string>;
}

/**
 * Information about the WNS push notification channel for this application.
 */
export interface WnsChannel {
  /** The WNS channel URI to which push notifications should be sent. */
  uri: string;
  /**
   * The channel expiration time expressed as Windows FILETIME ticks
   * (100-nanosecond intervals elapsed since 1 January 1601 UTC).
   */
  expirationTicks: number;
}

/**
 * Requests a WNS push notification channel for the application.
 * The channel is cached for the lifetime of the process; subsequent calls
 * return the same channel without contacting the WNS service again.
 *
 * @returns A promise that resolves with the channel information.
 */
export function getChannel(): Promise<WnsChannel>;

/**
 * Registers a callback to receive WNS push notifications while the application
 * is running in the foreground. Calling this function a second time replaces
 * the previously registered callback.
 *
 * @param callback - Function invoked for each incoming notification.
 */
export function startForegroundNotifications(
  callback: (notification: WnsNotification) => void
): void;

/**
 * Unregisters the foreground notification callback and stops delivering
 * notifications. Has no effect if notifications were not started.
 */
export function stopForegroundNotifications(): void;

/**
 * Returns whether WinAppSDK push notifications are available in the current runtime.
 * This is useful for determining whether unpackaged push APIs can be used.
 */
export function isWinAppSdkPushSupported(): boolean;

/**
 * Runtime diagnostics for deciding whether WinAppSDK push path can be used.
 */
export interface WinAppSdkPushDiagnostics {
  compiledWithWinAppSdkHeaders: boolean;
  isSupported: boolean;
  errorCode: number;
  errorMessage: string;
}

/**
 * Returns WinAppSDK push runtime diagnostics and any captured support error.
 */
export function getWinAppSdkPushDiagnostics(): WinAppSdkPushDiagnostics;

/**
 * Registers the process with PushNotificationManager and enables foreground
 * delivery for push notifications handled by WinAppSDK.
 */
export function registerPush(): void;

/**
 * Unregisters the process from PushNotificationManager.
 */
export function unregisterPush(): void;

/**
 * Requests a WNS channel URI via WinAppSDK PushNotificationManager using a
 * remoteId GUID string.
 *
 * @param remoteId - GUID string used when creating the channel.
 */
export function getChannelForPushManager(remoteId: string): Promise<WnsChannel>;
