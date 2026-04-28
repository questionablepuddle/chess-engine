import { spawn, ChildProcess } from 'child_process';
import * as readline from 'readline';
import * as path from 'path';
import { sleep, log } from './utils';

// ---------------------------------------------------------------------------
// UCI Engine wrapper
// Spawns the engine binary as a persistent child process and communicates
// over stdin/stdout using the UCI protocol.
// ---------------------------------------------------------------------------

export class UCIEngine {
  private proc: ChildProcess | null = null;
  private rl: readline.Interface | null = null;

  // Lines received since last clearBuffer()
  private buffer: string[] = [];

  // One active waiter at a time (we always await sequentially)
  private waiter: {
    token: string;
    resolve: (lines: string[]) => void;
    reject: (e: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  } | null = null;

  readonly enginePath: string;

  constructor(enginePath: string) {
    this.enginePath = path.resolve(enginePath);
  }

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------

  async init(): Promise<void> {
    await this.spawnProc();
    await this.cmd('uci', 'uciok', 5_000);
    this.write('setoption name MultiPV value 3');
    await this.cmd('isready', 'readyok', 5_000);
    log('Engine', `Ready  (${path.basename(this.enginePath)})`);
  }

  async newGame(): Promise<void> {
    this.write('ucinewgame');
    await this.cmd('isready', 'readyok', 5_000);
    log('Engine', 'New game ready');
  }

  // Ask for the best move given a FEN string (or "startpos moves ..." fallback).
  async bestMove(fen: string, moveTimeMs = 1_000): Promise<string> {
    // If the caller passed a startpos fallback string, use it directly;
    // otherwise wrap in "position fen ..."
    const posCmd = fen.startsWith('startpos')
      ? `position ${fen}`
      : `position fen ${fen}`;
    this.write(posCmd);
    const lines = await this.cmd(
      `go movetime ${moveTimeMs}`,
      'bestmove',
      moveTimeMs + 8_000,
    );

    const line = lines.find(l => l.startsWith('bestmove'));
    if (!line) throw new Error('Engine did not return bestmove');

    const move = line.split(' ')[1];
    if (!move || move === '(none)') throw new Error('Engine returned (none)');

    log('Engine', `bestmove ${move}  (fen: ${fen})`);
    return move;
  }

  async restart(): Promise<void> {
    log('Engine', 'Restarting process…');
    this.killProc();
    await this.spawnProc();
    await this.cmd('uci', 'uciok', 5_000);
    await this.cmd('isready', 'readyok', 5_000);
    log('Engine', 'Restarted');
  }

  quit(): void {
    this.write('quit');
    setTimeout(() => this.killProc(), 500);
  }

  // ---------------------------------------------------------------------------
  // Internal helpers
  // ---------------------------------------------------------------------------

  private async spawnProc(): Promise<void> {
    this.killProc();

    this.proc = spawn(this.enginePath, [], { stdio: ['pipe', 'pipe', 'pipe'] });

    this.proc.on('error', err => log('Engine', `Spawn error: ${err.message}`));
    this.proc.on('exit', code => log('Engine', `Process exited (${code})`));
    this.proc.stderr?.on('data', () => {}); // discard engine debug output

    this.rl = readline.createInterface({ input: this.proc.stdout! });
    this.rl.on('line', (line: string) => this.onLine(line));

    await sleep(150); // give the process a moment to start
  }

  private killProc(): void {
    try {
      this.rl?.close();
      if (this.proc && !this.proc.killed) this.proc.kill('SIGTERM');
    } catch {}
    this.proc = null;
    this.rl = null;
  }

  private write(cmd: string): void {
    if (!this.proc?.stdin || this.proc.killed) throw new Error('Engine not running');
    this.proc.stdin.write(cmd + '\n');
  }

  // Send a command and wait until a line starting with `token` arrives.
  private cmd(command: string, token: string, timeoutMs: number): Promise<string[]> {
    if (this.waiter) {
      // Shouldn't happen in normal sequential use; clean up stale waiter
      clearTimeout(this.waiter.timer);
      this.waiter.reject(new Error('Superseded by new command'));
      this.waiter = null;
    }

    this.buffer = [];

    return new Promise<string[]>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiter = null;
        reject(new Error(`Engine timeout waiting for "${token}" (${timeoutMs}ms)`));
      }, timeoutMs);

      this.waiter = { token, resolve, reject, timer };
      this.write(command);
    });
  }

  private onLine(raw: string): void {
    const line = raw.trim();
    if (!line) return;

    this.buffer.push(line);

    if (this.waiter && line.startsWith(this.waiter.token)) {
      clearTimeout(this.waiter.timer);
      const { resolve } = this.waiter;
      this.waiter = null;
      resolve([...this.buffer]);
    }
  }
}
