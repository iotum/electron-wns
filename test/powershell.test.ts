/**
 * Tests for the PowerShell execution utilities in src/powershell.ts.
 * The child_process.spawn call is mocked so no real PowerShell is needed.
 */

import { EventEmitter } from 'events';
import { spawnPowershell, runPowershell, defaultPowershellPath } from '../src/powershell';

// ---------------------------------------------------------------------------
// Minimal mock of a ChildProcess
// ---------------------------------------------------------------------------
class MockStream extends EventEmitter {
  setEncoding = jest.fn();
}

class MockChildProcess extends EventEmitter {
  stdout = new MockStream();
  stderr = new MockStream();
  kill = jest.fn();
}

jest.mock('child_process', () => ({
  spawn: jest.fn(),
}));

import { spawn } from 'child_process';
const mockSpawn = spawn as jest.MockedFunction<typeof spawn>;

// Helper to produce a mock ChildProcess that emits the given stdout lines,
// optional stderr, then exits with `exitCode`.
function mockProcess(
  stdoutLines: string[],
  exitCode: number,
  stderrLines: string[] = [],
): MockChildProcess {
  const proc = new MockChildProcess();
  mockSpawn.mockReturnValueOnce(proc as unknown as ReturnType<typeof spawn>);

  setImmediate(() => {
    for (const line of stderrLines) {
      proc.stderr.emit('data', line);
    }
    for (const line of stdoutLines) {
      proc.stdout.emit('data', line + '\n');
    }
    proc.emit('close', exitCode);
  });

  return proc;
}

// ---------------------------------------------------------------------------
// defaultPowershellPath
// ---------------------------------------------------------------------------
describe('defaultPowershellPath', () => {
  it('returns powershell.exe on win32', () => {
    const origPlatform = process.platform;
    Object.defineProperty(process, 'platform', { value: 'win32', configurable: true });
    expect(defaultPowershellPath()).toBe('powershell.exe');
    Object.defineProperty(process, 'platform', { value: origPlatform, configurable: true });
  });

  it('returns pwsh on non-win32', () => {
    const origPlatform = process.platform;
    Object.defineProperty(process, 'platform', { value: 'linux', configurable: true });
    expect(defaultPowershellPath()).toBe('pwsh');
    Object.defineProperty(process, 'platform', { value: origPlatform, configurable: true });
  });
});

// ---------------------------------------------------------------------------
// spawnPowershell
// ---------------------------------------------------------------------------
describe('spawnPowershell', () => {
  beforeEach(() => mockSpawn.mockReset());

  it('parses newline-delimited JSON messages from stdout', (done) => {
    const messages: object[] = [];
    mockProcess(
      ['{"type":"channel","uri":"https://example.com"}'],
      0,
    );

    spawnPowershell({
      powershellPath: 'pwsh',
      scriptPath: '/fake/script.ps1',
      onMessage: (msg) => messages.push(msg),
      onError: () => {},
      onExit: () => {
        expect(messages).toHaveLength(1);
        expect(messages[0]).toEqual({ type: 'channel', uri: 'https://example.com' });
        done();
      },
    });
  });

  it('ignores non-JSON lines', (done) => {
    const messages: object[] = [];
    mockProcess(
      ['not json', '{"type":"notification","payload":"hello"}'],
      0,
    );

    spawnPowershell({
      powershellPath: 'pwsh',
      scriptPath: '/fake/script.ps1',
      onMessage: (msg) => messages.push(msg),
      onError: () => {},
      onExit: () => {
        expect(messages).toHaveLength(1);
        done();
      },
    });
  });

  it('calls onError for stderr data', (done) => {
    const errors: Error[] = [];
    mockProcess([], 0, ['Something went wrong']);

    spawnPowershell({
      powershellPath: 'pwsh',
      scriptPath: '/fake/script.ps1',
      onMessage: () => {},
      onError: (err) => errors.push(err),
      onExit: () => {
        expect(errors).toHaveLength(1);
        expect(errors[0].message).toContain('Something went wrong');
        done();
      },
    });
  });

  it('handles multi-line chunks correctly', (done) => {
    const proc = new MockChildProcess();
    mockSpawn.mockReturnValueOnce(proc as unknown as ReturnType<typeof spawn>);

    const messages: object[] = [];

    spawnPowershell({
      powershellPath: 'pwsh',
      scriptPath: '/fake/script.ps1',
      onMessage: (msg) => messages.push(msg),
      onError: () => {},
      onExit: () => {
        expect(messages).toHaveLength(2);
        done();
      },
    });

    setImmediate(() => {
      // Two messages arrive in a single chunk
      proc.stdout.emit('data', '{"type":"channel","uri":"a"}\n{"type":"notification","payload":"b"}\n');
      proc.emit('close', 0);
    });
  });
});

// ---------------------------------------------------------------------------
// runPowershell
// ---------------------------------------------------------------------------
describe('runPowershell', () => {
  beforeEach(() => mockSpawn.mockReset());

  it('resolves with all messages on exit code 0', async () => {
    mockProcess(
      ['{"type":"channel","uri":"https://wns.example.com","expiresAt":"2026-01-01T00:00:00Z"}'],
      0,
    );

    const messages = await runPowershell('pwsh', '/fake/get-channel.ps1');
    expect(messages).toHaveLength(1);
    expect(messages[0].type).toBe('channel');
  });

  it('rejects on non-zero exit code', async () => {
    mockProcess(
      ['{"type":"error","message":"Channel registration failed"}'],
      1,
    );

    await expect(runPowershell('pwsh', '/fake/get-channel.ps1')).rejects.toThrow(
      'PowerShell exited with code 1',
    );
  });
});
