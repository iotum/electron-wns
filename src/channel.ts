/**
 * WNS channel management – obtaining and caching the push notification channel URI.
 */

import * as path from 'path';
import { runPowershell } from './powershell';
import { WNSChannel } from './types';

/** Resolve the path to a bundled PowerShell script. */
function scriptPath(name: string): string {
  return path.join(__dirname, '..', 'scripts', name);
}

/**
 * Request a push notification channel URI from WNS by running the
 * `get-channel.ps1` PowerShell helper.
 *
 * @throws {Error} If PowerShell fails or the channel URI cannot be obtained.
 */
export async function requestChannel(powershellPath: string): Promise<WNSChannel> {
  const messages = await runPowershell(powershellPath, scriptPath('get-channel.ps1'));

  for (const msg of messages) {
    if (msg.type === 'channel') {
      const uri = msg.uri;
      const expiresAt = msg.expiresAt;
      if (typeof uri !== 'string' || typeof expiresAt !== 'string') {
        throw new Error('Malformed channel message from PowerShell helper');
      }
      return { uri, expiresAt };
    }
    if (msg.type === 'error') {
      throw new Error(String(msg.message ?? 'Unknown error from PowerShell helper'));
    }
  }

  throw new Error('No channel message received from PowerShell helper');
}
