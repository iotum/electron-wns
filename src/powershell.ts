/**
 * Utilities for spawning PowerShell and parsing its newline-delimited JSON output.
 */

import { spawn, ChildProcess } from 'child_process';

/** A raw message envelope written by the PowerShell helper. */
export interface PSMessage {
  type: 'channel' | 'notification' | 'error';
  [key: string]: unknown;
}

/** Options for {@link spawnPowershell}. */
export interface SpawnPowershellOptions {
  /** Path to powershell executable. */
  powershellPath: string;
  /** Absolute path to the `.ps1` script to run. */
  scriptPath: string;
  /** Called for each complete JSON line written to stdout. */
  onMessage: (msg: PSMessage) => void;
  /** Called when the process writes to stderr. */
  onError: (err: Error) => void;
  /** Called when the process exits. */
  onExit: (code: number | null) => void;
}

/**
 * Spawn a PowerShell process running `scriptPath` and parse its newline-delimited
 * JSON stdout into {@link PSMessage} objects.
 *
 * Returns the {@link ChildProcess} so the caller can stop it.
 */
export function spawnPowershell(opts: SpawnPowershellOptions): ChildProcess {
  const ps = spawn(
    opts.powershellPath,
    [
      '-NonInteractive',
      '-NoProfile',
      '-ExecutionPolicy', 'Bypass',
      '-File', opts.scriptPath,
    ],
    { stdio: ['ignore', 'pipe', 'pipe'] },
  );

  let buffer = '';

  ps.stdout!.setEncoding('utf8');
  ps.stdout!.on('data', (chunk: string) => {
    buffer += chunk;
    const lines = buffer.split(/\r?\n/);
    buffer = lines.pop() ?? '';
    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      try {
        const msg = JSON.parse(trimmed) as PSMessage;
        opts.onMessage(msg);
      } catch {
        // Ignore non-JSON lines (e.g. debug output from PowerShell)
      }
    }
  });

  ps.stderr!.setEncoding('utf8');
  ps.stderr!.on('data', (chunk: string) => {
    opts.onError(new Error(chunk.trim()));
  });

  ps.on('close', (code) => {
    opts.onExit(code);
  });

  return ps;
}

/**
 * Run a PowerShell script, collect all stdout, and resolve with the parsed
 * array of {@link PSMessage} objects.  Rejects if the process exits non-zero.
 */
export function runPowershell(
  powershellPath: string,
  scriptPath: string,
): Promise<PSMessage[]> {
  return new Promise((resolve, reject) => {
    const messages: PSMessage[] = [];
    const proc = spawnPowershell({
      powershellPath,
      scriptPath,
      onMessage: (msg) => messages.push(msg),
      onError: (err) => {
        // Collect stderr but don't reject yet – wait for exit code
        messages.push({ type: 'error', message: err.message });
      },
      onExit: (code) => {
        if (code !== 0) {
          const errMsg = messages
            .filter((m) => m.type === 'error')
            .map((m) => String(m.message))
            .join(' ');
          reject(new Error(`PowerShell exited with code ${code}: ${errMsg}`));
        } else {
          resolve(messages);
        }
      },
    });

    // Keep a reference so the GC doesn't collect the process
    void proc;
  });
}

/** Return the default powershell executable name for the current platform. */
export function defaultPowershellPath(): string {
  return process.platform === 'win32' ? 'powershell.exe' : 'pwsh';
}
