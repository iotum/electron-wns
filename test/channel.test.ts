/**
 * Tests for src/channel.ts – requestChannel()
 * The PowerShell execution is mocked via jest.mock.
 */

jest.mock('../src/powershell', () => ({
  runPowershell: jest.fn(),
}));

import { requestChannel } from '../src/channel';
import { runPowershell } from '../src/powershell';

const mockRunPowershell = runPowershell as jest.MockedFunction<typeof runPowershell>;

describe('requestChannel', () => {
  beforeEach(() => mockRunPowershell.mockReset());

  it('returns a WNSChannel when PowerShell emits a channel message', async () => {
    mockRunPowershell.mockResolvedValueOnce([
      {
        type: 'channel',
        uri: 'https://dm3p.notify.windows.com/?token=abc123',
        expiresAt: '2026-04-01T00:00:00Z',
      },
    ]);

    const channel = await requestChannel('powershell.exe');

    expect(channel.uri).toBe('https://dm3p.notify.windows.com/?token=abc123');
    expect(channel.expiresAt).toBe('2026-04-01T00:00:00Z');
  });

  it('throws when PowerShell emits an error message', async () => {
    mockRunPowershell.mockResolvedValueOnce([
      { type: 'error', message: 'Access is denied.' },
    ]);

    await expect(requestChannel('powershell.exe')).rejects.toThrow('Access is denied.');
  });

  it('throws when PowerShell returns no channel message', async () => {
    mockRunPowershell.mockResolvedValueOnce([]);

    await expect(requestChannel('powershell.exe')).rejects.toThrow(
      'No channel message received',
    );
  });

  it('throws when the channel message is malformed', async () => {
    mockRunPowershell.mockResolvedValueOnce([
      { type: 'channel', uri: 42, expiresAt: null },
    ]);

    await expect(requestChannel('powershell.exe')).rejects.toThrow('Malformed channel message');
  });

  it('propagates PowerShell spawn errors', async () => {
    mockRunPowershell.mockRejectedValueOnce(new Error('PowerShell exited with code 1: spawn error'));

    await expect(requestChannel('powershell.exe')).rejects.toThrow('spawn error');
  });
});
