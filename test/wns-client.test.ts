/**
 * Tests for WNSClient (src/wns-client.ts)
 *
 * Both the channel helper and the PowerShell spawner are mocked so no
 * real PowerShell or WNS infrastructure is required.
 */

import { EventEmitter } from 'events';

// ---------------------------------------------------------------------------
// Mock channel.ts
// ---------------------------------------------------------------------------
jest.mock('../src/channel', () => ({
  requestChannel: jest.fn(),
}));

// ---------------------------------------------------------------------------
// Mock powershell.ts – spawnPowershell returns a fake ChildProcess
// ---------------------------------------------------------------------------
class FakeChildProcess extends EventEmitter {
  stdout = new EventEmitter();
  stderr = new EventEmitter();
  kill = jest.fn(() => {
    this.emit('close', 0);
  });
}

let fakeListenerProc: FakeChildProcess;

jest.mock('../src/powershell', () => {
  const original = jest.requireActual('../src/powershell');
  return {
    ...original,
    spawnPowershell: jest.fn(),
  };
});

import { WNSClient } from '../src/wns-client';
import { requestChannel } from '../src/channel';
import { spawnPowershell } from '../src/powershell';

const mockRequestChannel = requestChannel as jest.MockedFunction<typeof requestChannel>;
const mockSpawnPowershell = spawnPowershell as jest.MockedFunction<typeof spawnPowershell>;

const FAKE_CHANNEL = {
  uri: 'https://dm3p.notify.windows.com/?token=test',
  expiresAt: '2026-04-01T00:00:00Z',
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function setupListenerMock(): FakeChildProcess {
  fakeListenerProc = new FakeChildProcess();
  mockSpawnPowershell.mockImplementationOnce((opts) => {
    setImmediate(() => {
      // Emit a channel message and a notification message
      opts.onMessage({ type: 'channel', uri: FAKE_CHANNEL.uri, expiresAt: FAKE_CHANNEL.expiresAt });
      opts.onMessage({
        type: 'notification',
        notificationType: 'raw',
        payload: 'hello world',
        timestamp: '2026-01-01T00:00:00Z',
      });
    });
    return fakeListenerProc as unknown as ReturnType<typeof spawnPowershell>;
  });
  return fakeListenerProc;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
describe('WNSClient', () => {
  beforeEach(() => {
    mockRequestChannel.mockReset();
    mockSpawnPowershell.mockReset();
  });

  // ── getChannel ────────────────────────────────────────────────────────────
  describe('getChannel', () => {
    it('returns the channel from requestChannel', async () => {
      mockRequestChannel.mockResolvedValueOnce(FAKE_CHANNEL);
      const client = new WNSClient();
      const ch = await client.getChannel();
      expect(ch).toEqual(FAKE_CHANNEL);
    });

    it('caches the channel on subsequent calls', async () => {
      mockRequestChannel.mockResolvedValueOnce(FAKE_CHANNEL);
      const client = new WNSClient();
      await client.getChannel();
      await client.getChannel();
      expect(mockRequestChannel).toHaveBeenCalledTimes(1);
    });

    it('propagates errors from requestChannel', async () => {
      mockRequestChannel.mockRejectedValueOnce(new Error('Access denied'));
      const client = new WNSClient();
      await expect(client.getChannel()).rejects.toThrow('Access denied');
    });
  });

  // ── refreshChannel ────────────────────────────────────────────────────────
  describe('refreshChannel', () => {
    it('emits channelUpdated with the new channel', async () => {
      const updated = { uri: 'https://new.example.com/', expiresAt: '2027-01-01T00:00:00Z' };
      mockRequestChannel
        .mockResolvedValueOnce(FAKE_CHANNEL)
        .mockResolvedValueOnce(updated);

      const client = new WNSClient();
      await client.getChannel();

      const events: unknown[] = [];
      client.on('channelUpdated', (ch) => events.push(ch));
      await client.refreshChannel();

      expect(events).toHaveLength(1);
      expect(events[0]).toEqual(updated);
    });
  });

  // ── startListening / stopListening ────────────────────────────────────────
  describe('startListening', () => {
    it('emits channelUpdated when the listener reports a channel', (done) => {
      setupListenerMock();
      const client = new WNSClient();
      client.on('channelUpdated', (ch) => {
        expect(ch.uri).toBe(FAKE_CHANNEL.uri);
        client.stopListening();
        done();
      });
      client.startListening();
    });

    it('emits notification when the listener reports a notification', (done) => {
      setupListenerMock();
      const client = new WNSClient();
      client.on('notification', (n) => {
        expect(n.notificationType).toBe('raw');
        expect(n.payload).toBe('hello world');
        client.stopListening();
        done();
      });
      client.startListening();
    });

    it('is a no-op when already listening', () => {
      setupListenerMock();
      const client = new WNSClient();
      client.startListening();
      client.startListening(); // second call should be no-op
      expect(mockSpawnPowershell).toHaveBeenCalledTimes(1);
      client.stopListening();
    });

    it('emits error for error messages from listener', (done) => {
      fakeListenerProc = new FakeChildProcess();
      mockSpawnPowershell.mockImplementationOnce((opts) => {
        setImmediate(() => {
          opts.onMessage({ type: 'error', message: 'Listener failed' });
        });
        return fakeListenerProc as unknown as ReturnType<typeof spawnPowershell>;
      });

      const client = new WNSClient();
      client.on('error', (err) => {
        expect(err.message).toContain('Listener failed');
        client.stopListening();
        done();
      });
      client.startListening();
    });
  });

  describe('stopListening', () => {
    it('kills the listener process and sets isListening to false', () => {
      setupListenerMock();
      const client = new WNSClient();
      client.startListening();
      expect(client.isListening()).toBe(true);
      client.stopListening();
      expect(client.isListening()).toBe(false);
    });

    it('is safe to call when not listening', () => {
      const client = new WNSClient();
      expect(() => client.stopListening()).not.toThrow();
    });
  });

  // ── isListening ───────────────────────────────────────────────────────────
  describe('isListening', () => {
    it('returns false before startListening', () => {
      const client = new WNSClient();
      expect(client.isListening()).toBe(false);
    });

    it('returns true after startListening', () => {
      setupListenerMock();
      const client = new WNSClient();
      client.startListening();
      expect(client.isListening()).toBe(true);
      client.stopListening();
    });
  });

  // ── powershellPath option ─────────────────────────────────────────────────
  describe('constructor options', () => {
    it('passes custom powershellPath to requestChannel', async () => {
      mockRequestChannel.mockResolvedValueOnce(FAKE_CHANNEL);
      const client = new WNSClient({ powershellPath: 'C:\\Windows\\pwsh.exe' });
      await client.getChannel();
      expect(mockRequestChannel).toHaveBeenCalledWith('C:\\Windows\\pwsh.exe');
    });
  });
});
