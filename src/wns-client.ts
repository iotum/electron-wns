/**
 * WNSClient – the main entry point for receiving WNS push notifications in an
 * Electron main process.
 *
 * Usage:
 * ```ts
 * import { WNSClient } from 'electron-wns';
 *
 * const client = new WNSClient();
 *
 * const channel = await client.getChannel();
 * console.log('Send this URI to your backend:', channel.uri);
 *
 * client.on('notification', (n) => console.log('Push received:', n.payload));
 * client.startListening();
 * ```
 */

import { EventEmitter } from 'events';
import * as path from 'path';
import { ChildProcess } from 'child_process';
import { spawnPowershell, defaultPowershellPath, PSMessage } from './powershell';
import { requestChannel } from './channel';
import { WNSChannel, WNSClientOptions, WNSNotification, WNSNotificationType } from './types';

export class WNSClient extends EventEmitter {
  private readonly powershellPath: string;
  private cachedChannel: WNSChannel | null = null;
  private listenerProcess: ChildProcess | null = null;

  constructor(options: WNSClientOptions = {}) {
    super();
    this.powershellPath = options.powershellPath ?? defaultPowershellPath();
  }

  // ── Channel ────────────────────────────────────────────────────────────────

  /**
   * Obtain a WNS push notification channel for this application.
   *
   * The result is cached in memory; call `refreshChannel()` to force a
   * fresh request (e.g. after the channel expiry date has passed).
   *
   * @throws {Error} On Windows if the app does not have a package identity
   *   (MSIX packaging is required for `PushNotificationChannelManager`).
   */
  async getChannel(): Promise<WNSChannel> {
    if (!this.cachedChannel) {
      this.cachedChannel = await requestChannel(this.powershellPath);
    }
    return this.cachedChannel;
  }

  /**
   * Force a new channel request and update the internal cache.
   * Emits `channelUpdated` with the new channel.
   */
  async refreshChannel(): Promise<WNSChannel> {
    this.cachedChannel = await requestChannel(this.powershellPath);
    this.emit('channelUpdated', this.cachedChannel);
    return this.cachedChannel;
  }

  // ── Listening ──────────────────────────────────────────────────────────────

  /**
   * Start listening for incoming push notifications.
   *
   * Spawns a PowerShell background process (`wns-listener.ps1`) that
   * registers for `PushNotificationReceived` events on the WNS channel and
   * pipes them back as newline-delimited JSON.
   *
   * Emits:
   * - `channelUpdated` once the channel has been registered
   * - `notification` for each incoming push
   * - `error` for non-fatal errors (the listener continues running)
   *
   * Calling `startListening()` while already listening is a no-op.
   */
  startListening(): void {
    if (this.listenerProcess) return;

    const scriptFile = path.join(__dirname, '..', 'scripts', 'wns-listener.ps1');

    this.listenerProcess = spawnPowershell({
      powershellPath: this.powershellPath,
      scriptPath: scriptFile,
      onMessage: (msg: PSMessage) => this.handleListenerMessage(msg),
      onError: (err: Error) => this.emit('error', err),
      onExit: (code: number | null) => {
        this.listenerProcess = null;
        if (code !== 0 && code !== null) {
          this.emit('error', new Error(`WNS listener exited with code ${code}`));
        }
      },
    });
  }

  /**
   * Stop the background listener.  Safe to call even if not currently
   * listening.
   */
  stopListening(): void {
    if (this.listenerProcess) {
      this.listenerProcess.kill();
      this.listenerProcess = null;
    }
  }

  /** Returns `true` if the background listener is currently running. */
  isListening(): boolean {
    return this.listenerProcess !== null;
  }

  // ── Internal ───────────────────────────────────────────────────────────────

  private handleListenerMessage(msg: PSMessage): void {
    switch (msg.type) {
      case 'channel': {
        const uri = String(msg.uri ?? '');
        const expiresAt = String(msg.expiresAt ?? '');
        if (uri) {
          const channel: WNSChannel = { uri, expiresAt };
          this.cachedChannel = channel;
          this.emit('channelUpdated', channel);
        }
        break;
      }
      case 'notification': {
        const notification: WNSNotification = {
          notificationType: (msg.notificationType as WNSNotificationType) ?? 'raw',
          payload: String(msg.payload ?? ''),
          timestamp: String(msg.timestamp ?? new Date().toISOString()),
        };
        this.emit('notification', notification);
        break;
      }
      case 'error': {
        this.emit('error', new Error(String(msg.message ?? 'Unknown listener error')));
        break;
      }
    }
  }
}
