import { webkit, Browser, BrowserContext, Page } from 'playwright';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { sleep, humanDelay, randomBetween, log } from './utils';

const COOKIES_PATH = path.join(os.homedir(), '.chess-bot-profile', 'cookies.json');

// ---------------------------------------------------------------------------
// Selector constants
// chess.com updates their DOM periodically; centralising selectors here makes
// fixes a one-liner.  Entries are tried in order; first visible one wins.
// ---------------------------------------------------------------------------

const SEL = {
  // Board container (used for orientation detection)
  board: [
    'chess-board',
    '#board-vs-machine',
    '#board-layout-board',
    '.board-layout-board',
    '.board',
  ],

  // Individual square element inside the board
  square: (cls: string) => [
    `[class*="${cls}"]`,
    `chess-board >> [class*="${cls}"]`,
    `#board-layout-board [class*="${cls}"]`,
  ],

  // Promotion dialog — appears when a pawn reaches the last rank
  promotionDialog: [
    '[class*="promotion-popup"]',
    '[class*="promotion"]',
    '.promotion',
  ],
  // Queen piece inside the promotion dialog
  promotionQueen: (colorPrefix: string) => [
    `[class*="promotion"] [class*="${colorPrefix}q"]`,
    `[class*="promotion"] .${colorPrefix}q`,
    `[class*="promotion"] >> [class*="queen"]`,
    `[class*="promotion"] >> :nth-child(1)`, // queen is always first
  ],

  // SAN move text nodes in the move list (read full game history from here)
  moveNodes: [
    // Newer UI — wc-simple-move-list web component (shadow DOM pierced by Playwright)
    'wc-simple-move-list .node',
    // Classic vertical move list
    '.vertical-move-list .node',
    // Move list with move-node classes
    '.moves .move-node',
    // Generic: any element labelled node inside a moves wrapper
    '[class*="move-list"] [class*="node"]',
  ],

  // Game-over overlay / modal
  gameOver: [
    '[class*="game-over"]',
    '[class*="GameOver"]',
    '.game-over-modal',
    '[class*="result-modal"]',
    '.result-wrap',
  ],

  // "Play again" / "New Game" button after game ends (unused — we navigate fresh)
  newGame: [
    'button:has-text("New Game")',
    'button:has-text("Play Again")',
    'button:has-text("Rematch")',
  ],

  // Play button in the right panel after selecting a bot
  playButton: [
    'button[class*="play"]:visible',
    'button:has-text("Play"):visible',
    'button:has-text("Play Game"):visible',
    'button:has-text("Challenge"):visible',
    'button:has-text("Start"):visible',
  ],
};

// ---------------------------------------------------------------------------
// ChessDotCom — main browser automation class
// ---------------------------------------------------------------------------

export class ChessDotCom {
  private browser!: Browser;
  private context!: BrowserContext;
  page!: Page;

  ourColor: 'white' | 'black' = 'white';

  // Full move history accumulated across DOM scroll events.
  // DOM only shows the last ~4 visible moves; this keeps the complete list.
  private allMoves: string[] = [];

  // ---------------------------------------------------------------------------
  // Initialisation — WebKit (Safari engine) with saved cookie persistence
  // ---------------------------------------------------------------------------

  async init(): Promise<void> {
    log('Browser', 'Launching WebKit (Safari)…');

    this.browser = await webkit.launch({ headless: false });

    this.context = await this.browser.newContext({
      userAgent:
        'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) ' +
        'AppleWebKit/605.1.15 (KHTML, like Gecko) ' +
        'Version/17.0 Safari/605.1.15',
    });

    // Load saved cookies if they exist
    if (fs.existsSync(COOKIES_PATH)) {
      const saved = JSON.parse(fs.readFileSync(COOKIES_PATH, 'utf8'));
      await this.context.addCookies(saved);
      log('Browser', `Loaded ${saved.length} cookies from ${COOKIES_PATH}`);
    }

    this.page = await this.context.newPage();

    log('Browser', 'Navigating to chess.com…');
    try {
      await this.page.goto('https://www.chess.com/', {
        waitUntil: 'domcontentloaded',
        timeout: 30_000,
      });
      log('Browser', `URL: ${this.page.url()}`);
    } catch (err) {
      log('Browser', `ERROR: goto() failed — keeping browser open: ${err}`);
      throw err;
    }

    await this.ensureLoggedIn();
  }

  // ---------------------------------------------------------------------------
  // Login — manual flow with cookie save for subsequent runs
  // ---------------------------------------------------------------------------

  private async ensureLoggedIn(): Promise<void> {
    await sleep(1500);

    if (await this.isLoggedIn()) {
      log('Browser', 'Already logged in');
      return;
    }

    while (true) {
      console.log('\n========================================');
      console.log('Please log in to chess.com in the browser window,');
      console.log('then press ENTER in this terminal when done...');
      console.log('========================================\n');

      await new Promise<void>(resolve => process.stdin.once('data', () => resolve()));

      await this.page.reload({ waitUntil: 'domcontentloaded' });
      await sleep(1500);

      if (await this.isLoggedIn()) {
        log('Browser', 'Login confirmed — saving cookies');
        const cookies = await this.context.cookies();
        fs.mkdirSync(path.dirname(COOKIES_PATH), { recursive: true });
        fs.writeFileSync(COOKIES_PATH, JSON.stringify(cookies, null, 2));
        log('Browser', `Cookies saved to ${COOKIES_PATH}`);
        break;
      }

      console.log('Not logged in yet — please try again.');
    }
  }

  private async isLoggedIn(): Promise<boolean> {
    const loggedInSelectors = [
      '[data-user-username]',
      '.user-username-component',
      'a[href*="/member/"]',
      '[class*="user-tagline-username"]',
      '[class*="header-user-tagline"]',
      'a[class*="user-tagline"]',
    ];
    for (const sel of loggedInSelectors) {
      try {
        const visible = await this.page.locator(sel).first().isVisible({ timeout: 2_000 });
        if (visible) {
          log('Browser', `Logged-in indicator: ${sel}`);
          return true;
        }
      } catch {}
    }
    return false;
  }

  // ---------------------------------------------------------------------------
  // Navigate to computer play page and start game vs selected bot
  // ---------------------------------------------------------------------------

  async startGameVsBot(): Promise<void> {
    this.allMoves = []; // reset move history for new game
    // chess.com is a SPA — goto() on the same URL doesn't re-render the React
    // app on game 2+. Navigate away first then back to force a fresh load.
    log('Browser', 'Navigating to chess.com/play/computer…');
    try {
      await this.page.goto('https://www.chess.com/', {
        waitUntil: 'domcontentloaded',
        timeout: 30_000,
      });
      await sleep(1_000);
      await this.page.goto('https://www.chess.com/play/computer', {
        waitUntil: 'domcontentloaded',
        timeout: 30_000,
      });
    } catch (err) {
      const msg = String(err).toLowerCase();
      if (msg.includes('crash') || msg.includes('target closed') || msg.includes('connection closed')) {
        log('Browser', `Page crash during navigation — relaunching context: ${err}`);
        await this.relaunchContext();
        await this.page.goto('https://www.chess.com/play/computer', {
          waitUntil: 'domcontentloaded',
          timeout: 30_000,
        });
      } else {
        throw err;
      }
    }
    await this.page.waitForLoadState('networkidle', { timeout: 15_000 }).catch(() => {
      log('Browser', 'networkidle timeout — continuing anyway');
    });
    // Wait for the bot panel to appear before interacting
    await this.page.waitForSelector('[class*="bot"], [class*="computer"]', { timeout: 10_000 })
      .catch(() => log('Browser', 'Bot panel selector not found — continuing anyway'));
    await sleep(5_000);
    await this.dismissPopups();

    // Give the SPA extra time to settle before interacting with bot selection
    await sleep(5_000);

    // --- Expand Beginner section if not already open ---
    log('Browser', 'Looking for Beginner header…');
    try {
      const header = this.page.locator('text="Beginner"').first();
      if (await header.isVisible({ timeout: 5_000 })) {
        await this.humanClick(header);
        log('Browser', 'Clicked Beginner header');
        await sleep(3_000);
      }
    } catch (e) {
      log('Browser', `Beginner header not found or already expanded: ${e}`);
    }

    // --- Find the first Beginner bot by Y-coordinate band ---
    // Locate the Beginner and Intermediate header Y positions, then find the
    // first sufficiently large <img> whose top sits between those two values.
    log('Browser', 'Finding first Beginner bot by coordinate…');
    const martinCenter = await this.page.evaluate(() => {
      // Find elements whose trimmed text is exactly "Beginner" or "Intermediate"
      // and have few children (to avoid matching wrappers that contain the text).
      const all = Array.from(document.querySelectorAll('*'));
      const headers = all.filter(
        el => el.children.length < 3 && el.textContent?.trim() === 'Beginner',
      );
      const nextHeaders = all.filter(
        el => el.children.length < 3 && el.textContent?.trim() === 'Intermediate',
      );

      const beginnerY = headers[0]?.getBoundingClientRect().top ?? -1;
      const intermediateY = nextHeaders[0]?.getBoundingClientRect().top ?? 9999;

      if (beginnerY < 0) return null;

      const imgs = Array.from(document.querySelectorAll('img')).filter(img => {
        const r = img.getBoundingClientRect();
        return r.top > beginnerY && r.top < intermediateY && r.width > 30;
      });

      const r = imgs[0]?.getBoundingClientRect();
      if (!r) return null;
      return { x: r.x + r.width / 2, y: r.y + r.height / 2, w: r.width, h: r.height };
    });

    if (!martinCenter) {
      await this.page.screenshot({ path: '/tmp/debug-bot-select.png' });
      throw new Error('Could not find first Beginner bot by coordinates — screenshot at /tmp/debug-bot-select.png');
    }

    log('Browser', `First Beginner bot center: ${JSON.stringify(martinCenter)}`);
    await this.page.mouse.click(martinCenter.x, martinCenter.y);

    await sleep(1_000);

    // --- Read selected bot name from right panel ---
    const selectedName = await this.page.evaluate(() => {
      const sels = [
        '[class*="bot-name"]', '[class*="computer-name"]',
        '[class*="panel"] h2', '[class*="panel"] h3',
        '[class*="sidebar"] h2', '[class*="sidebar"] h3',
        '[class*="opponent-name"]',
      ];
      for (const sel of sels) {
        const text = document.querySelector(sel)?.textContent?.trim();
        if (text) return text;
      }
      return '(unknown)';
    });
    log('Browser', `Selected bot: ${selectedName}`);

    await this.page.screenshot({ path: '/tmp/debug-bot-selected.png' });
    log('Browser', 'Bot selection screenshot: /tmp/debug-bot-selected.png');

    // --- Click Play ---
    const playBtn = await this.firstVisible(SEL.playButton, 6_000);
    if (playBtn) {
      await this.humanClick(playBtn);
      log('Browser', 'Clicked Play button');
    } else {
      log('Browser', 'No Play button found — game may start automatically');
    }

    // --- Wait for board ---
    await this.waitForBoard();

    await this.page.screenshot({ path: '/tmp/debug-game-start.png' });
    log('Browser', 'Game start screenshot: /tmp/debug-game-start.png');

    // --- Detect colour ---
    this.ourColor = await this.detectColor();
    log('Browser', `Playing as ${this.ourColor}`);
  }

  // ---------------------------------------------------------------------------
  // Board interaction
  // ---------------------------------------------------------------------------

  // Read the current FEN from chess.com's board object or DOM attributes.
  // Tries every known API surface and logs all results so we can see which works.
  async readFen(): Promise<string | null> {
    const probes = await this.page.evaluate(() => {
      const board = document.querySelector('chess-board') as any;

      const safe = (fn: () => any): any => {
        try { return fn() ?? null; } catch { return null; }
      };

      return {
        'board.game.getFEN()':            safe(() => board?.game?.getFEN?.()),
        'board.game.fen()':               safe(() => board?.game?.fen?.()),
        'board.getFen()':                 safe(() => board?.getFen?.()),
        'board.fen':                      safe(() => board?.fen),
        'board.getAttribute("fen")':      safe(() => board?.getAttribute?.('fen')),
        'board.position':                 safe(() => board?.position),
        'board.getAttribute("game").fen': safe(() => JSON.parse(board?.getAttribute?.('game') || '{}')?.fen),
        'window.chessboard.getFen()':     safe(() => (window as any).chessboard?.getFen?.()),
        '[fen] attr':                     safe(() => document.querySelector('[fen]')?.getAttribute('fen')),
      };
    });

    // Log every probe so we can see what chess.com exposes
    for (const [key, val] of Object.entries(probes)) {
      const display = val === null ? 'null' : String(val).slice(0, 100);
      log('Browser', `FEN probe [${key}]: ${display}`);
    }

    // Return the first value that looks like a FEN (contains slashes, 7+ parts)
    const isFen = (v: any): v is string =>
      typeof v === 'string' && v.split('/').length >= 7;

    for (const [key, val] of Object.entries(probes)) {
      if (isFen(val)) {
        log('Browser', `Using FEN from [${key}]: ${val}`);
        return val;
      }
    }

    log('Browser', 'FEN: all probes returned null — will fall back to SAN');
    return null;
  }

  // Returns the algebraic names of all highlighted squares (e.g. ["e2","e4"]).
  // Chess.com marks the last-move origin and target with a "highlight" CSS class.
  // These elements live in the light DOM, so they are always reachable.
  async readHighlightedSquares(): Promise<string[]> {
    try {
      const els = await this.page.locator('[class*="highlight"]').all();
      const squares: string[] = [];
      for (const el of els) {
        const cls = await el.getAttribute('class') || '';
        const m = cls.match(/\bsquare-([1-8])([1-8])\b/);
        if (m) {
          const sq = String.fromCharCode('a'.charCodeAt(0) + parseInt(m[1]) - 1) + m[2];
          if (!squares.includes(sq)) squares.push(sq);
        }
      }
      log('Browser', `Highlights: [${squares.join(', ')}]`);
      return squares;
    } catch (e) {
      log('Browser', `readHighlightedSquares error: ${e}`);
      return [];
    }
  }

  // Returns ALL SAN move strings from the move list (including scrolled-out moves).
  async readMoves(): Promise<string[]> {
    const moves = await this.page.evaluate(() => {
      // Scroll a container to the top so all moves are in the DOM,
      // then collect every text node that looks like a move.
      function scrollToTop(el: Element) {
        el.scrollTop = 0;
        // Also try inner scrollable children
        for (const child of Array.from(el.children)) {
          if ((child as HTMLElement).scrollHeight > (child as HTMLElement).clientHeight) {
            (child as HTMLElement).scrollTop = 0;
          }
        }
      }

      function collectFrom(root: Document | ShadowRoot): string[] {
        const sels = [
          'wc-simple-move-list .node',
          '.vertical-move-list .node',
          '.moves .move-node',
          '[class*="move-list"] [class*="node"]',
        ];
        for (const sel of sels) {
          try {
            // Scroll the parent container to expose all moves before reading
            const container = root.querySelector(
              'wc-simple-move-list, .vertical-move-list, .moves, [class*="move-list"]',
            );
            if (container) scrollToTop(container);

            const els = Array.from(root.querySelectorAll(sel));
            const texts = els
              .map(e => e.textContent?.trim() ?? '')
              .filter(t => t.length > 0 && !/^\d+\./.test(t));
            if (texts.length) return texts;
          } catch {}
        }

        // Pierce shadow roots of known web components
        for (const wc of ['wc-simple-move-list', 'vertical-move-list']) {
          const el = root.querySelector(wc);
          if (el instanceof HTMLElement) {
            scrollToTop(el);
            const sr = (el as any).shadowRoot as ShadowRoot | null;
            if (sr) {
              const inner = collectFrom(sr);
              if (inner.length) return inner;
            }
          }
        }

        return [];
      }

      return collectFrom(document);
    });

    return moves as string[];
  }

  // Reconcile DOM-visible moves with our in-memory full history and return
  // the complete list.  The DOM only shows the last ~4 moves when the list
  // scrolls; this keeps the entire game history intact.
  private reconcileMoves(visible: string[]): string[] {
    if (visible.length === 0) return this.allMoves;

    if (this.allMoves.length === 0) {
      this.allMoves = [...visible];
      return this.allMoves;
    }

    // Find the longest suffix of allMoves that matches a prefix of visible.
    // That overlap is the "already-known" part; anything after it is new.
    const maxOverlap = Math.min(this.allMoves.length, visible.length);
    for (let overlap = maxOverlap; overlap >= 1; overlap--) {
      const tail = this.allMoves.slice(this.allMoves.length - overlap);
      const head = visible.slice(0, overlap);
      if (tail.every((m, i) => m === head[i])) {
        const newMoves = visible.slice(overlap);
        if (newMoves.length > 0) {
          this.allMoves = [...this.allMoves, ...newMoves];
          log('Browser', `+${newMoves.length} move(s): ${newMoves.join(' ')}  total=${this.allMoves.length}`);
        }
        return this.allMoves;
      }
    }

    // No overlap at all → new game or complete desync; reset
    log('Browser', `Move list reset (no overlap). visible=[${visible.join(' ')}]`);
    this.allMoves = [...visible];
    return this.allMoves;
  }

  // Read DOM moves, reconcile with history, return full game move list.
  async syncMoves(): Promise<string[]> {
    const visible = await this.readMoves();
    return this.reconcileMoves(visible);
  }

  // Execute a UCI move (e.g. "e2e4", "e7e8q") on the board.
  // Returns true if the move registered (move count increased), false otherwise.
  async executeMove(uciMove: string, prevMoveCount: number): Promise<boolean> {
    const from = uciMove.slice(0, 2);
    const to = uciMove.slice(2, 4);
    const promo = uciMove.length === 5 ? uciMove[4] : undefined;

    log('Browser', `Executing move: ${uciMove}`);

    await this.clickSquare(from);
    await sleep(80 + randomBetween(40, 120));
    await this.clickSquare(to);

    if (promo) {
      await this.handlePromotion(promo);
    }

    // Wait up to 3s for the move to register
    const deadline = Date.now() + 3_000;
    while (Date.now() < deadline) {
      const current = (await this.readMoves()).length;
      if (current > prevMoveCount) return true;
      await sleep(100);
    }

    // Move did not register
    await this.page.screenshot({ path: '/tmp/debug-move-failed.png' });
    log('Browser', `Move ${uciMove} did not register — screenshot at /tmp/debug-move-failed.png`);
    return false;
  }

  // Returns true if the game is over (any result).
  async isGameOver(): Promise<boolean> {
    for (const sel of SEL.gameOver) {
      try {
        const el = this.page.locator(sel).first();
        if (await el.isVisible({ timeout: 200 })) return true;
      } catch {}
    }
    return false;
  }

  // Wait until it's our turn (move list length reaches expectedCount).
  // Throws 'GAME_OVER' if the game ends while waiting.
  async waitForOurTurn(expectedMoveCount: number, timeoutMs = 120_000): Promise<number> {
    const deadline = Date.now() + timeoutMs;
    const STUCK_MS = 30_000;
    let lastSeenCount = -1;
    let lastChangeAt = Date.now();

    while (Date.now() < deadline) {
      if (await this.isGameOver()) throw new Error('GAME_OVER');

      const moves = await this.syncMoves(); // reconciled full history
      log('Browser', `Move list (${moves.length}): …${moves.slice(-4).join(' ') || '(empty)'}`);

      if (moves.length !== lastSeenCount) {
        lastSeenCount = moves.length;
        lastChangeAt = Date.now();
      }

      if (moves.length >= expectedMoveCount) return moves.length;

      if (Date.now() - lastChangeAt > STUCK_MS) {
        await this.page.screenshot({ path: '/tmp/debug-stuck.png' });
        throw new Error(`Move list stuck at ${moves.length} for 30s — screenshot at /tmp/debug-stuck.png`);
      }

      await sleep(500);
    }
    throw new Error(`waitForOurTurn timed out after ${timeoutMs}ms`);
  }

  // ---------------------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------------------

  async close(): Promise<void> {
    await this.context?.close();
    await this.browser?.close();
  }

  // ---------------------------------------------------------------------------
  // Private helpers
  // ---------------------------------------------------------------------------

  // Close a crashed browser context and open a fresh one in the same process.
  // Reloads saved cookies so the session remains authenticated.
  private async relaunchContext(): Promise<void> {
    log('Browser', 'Closing crashed context and opening a fresh one…');
    try { await this.context.close(); } catch {}

    this.context = await this.browser.newContext({
      userAgent:
        'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) ' +
        'AppleWebKit/605.1.15 (KHTML, like Gecko) ' +
        'Version/17.0 Safari/605.1.15',
    });

    if (fs.existsSync(COOKIES_PATH)) {
      const saved = JSON.parse(fs.readFileSync(COOKIES_PATH, 'utf8'));
      await this.context.addCookies(saved);
      log('Browser', `Reloaded ${saved.length} cookies from ${COOKIES_PATH}`);
    }

    this.page = await this.context.newPage();
    log('Browser', 'Fresh context ready');
  }

  private async waitForBoard(): Promise<void> {
    log('Browser', 'Waiting for board…');
    const combined = '.board, chess-board, [class*="board"]';
    try {
      await this.page.waitForSelector(combined, { timeout: 15_000 });
      log('Browser', 'Board found');
      await sleep(500);
    } catch {
      await this.page.screenshot({ path: '/tmp/debug-board-load.png' });
      throw new Error('Chess board not found after 15s — screenshot at /tmp/debug-board-load.png');
    }
  }

  // Detect whether we're playing white or black by checking board orientation.
  private async detectColor(): Promise<'white' | 'black'> {
    const result = await this.page.evaluate(() => {
      // chess.com adds class "flipped" or attribute orientation="black" when
      // viewing the board from black's perspective.
      for (const sel of ['chess-board', '#board-vs-machine', '.board', '#board-layout-board']) {
        const el = document.querySelector(sel);
        if (!el) continue;
        const cls = el.className ?? '';
        const orientation = el.getAttribute('orientation') ?? el.getAttribute('player-color') ?? '';
        if (cls.includes('flipped') || orientation.includes('black')) return 'black';
        if (el) return 'white'; // found the board, no flip indicator
      }
      return 'white'; // default
    });
    return result as 'white' | 'black';
  }

  // Click a square by computing its center from the board's bounding box.
  // CSS class selectors (.square-XY) don't work inside the chess-board web
  // component, so coordinate-based clicking is the only reliable approach.
  private async clickSquare(sq: string): Promise<void> {
    const file = sq.charCodeAt(0) - 'a'.charCodeAt(0) + 1; // a=1 … h=8
    const rank = parseInt(sq[1], 10);                        // 1-8

    const boardEl = await this.page.$(SEL.board.join(', '));
    if (!boardEl) throw new Error(`Board element not found when clicking ${sq}`);
    const box = await boardEl.boundingBox();
    if (!box) throw new Error(`Board has no bounding box when clicking ${sq}`);

    const sqSize = box.width / 8;

    let x: number;
    let y: number;

    if (this.ourColor === 'white') {
      // a1 is bottom-left: file increases left→right, rank increases bottom→top
      x = box.x + (file - 1 + 0.5) * sqSize;
      y = box.y + (8 - rank + 0.5) * sqSize;
    } else {
      // Board is flipped: a1 is top-right, h8 is bottom-left
      x = box.x + (8 - file + 0.5) * sqSize;
      y = box.y + (rank - 1 + 0.5) * sqSize;
    }

    x += randomBetween(-3, 3);
    y += randomBetween(-3, 3);

    log('Browser', `Clicking ${sq} → (${Math.round(x)}, ${Math.round(y)})  [${this.ourColor}]`);

    await this.page.mouse.move(x, y, { steps: Math.ceil(randomBetween(3, 8)) });
    await sleep(40 + randomBetween(20, 60));
    await this.page.mouse.click(x, y);
  }

  // Handle the pawn promotion popup — always promotes to queen.
  private async handlePromotion(piece: string): Promise<void> {
    await sleep(500); // wait for dialog to appear
    const colorPrefix = this.ourColor === 'white' ? 'w' : 'b';

    for (const sel of SEL.promotionQueen(colorPrefix)) {
      try {
        const el = this.page.locator(sel).first();
        if (await el.isVisible({ timeout: 2_000 })) {
          await el.click();
          log('Browser', `Promotion → ${piece} (selected queen)`);
          return;
        }
      } catch {}
    }

    // Fallback: click the first item in any visible promotion dialog
    for (const dialogSel of SEL.promotionDialog) {
      try {
        const dialog = this.page.locator(dialogSel).first();
        if (await dialog.isVisible({ timeout: 1_000 })) {
          const firstPiece = dialog.locator(':nth-child(1)').first();
          if (await firstPiece.isVisible({ timeout: 1_000 })) {
            await firstPiece.click();
            return;
          }
        }
      } catch {}
    }

    log('Browser', 'WARNING: Could not find promotion dialog — move may be incomplete');
  }

  // Click the first visible element from a list of selectors.
  // Returns the Locator on success, or null if none are visible within timeoutMs.
  private async firstVisible(
    selectors: string[],
    timeoutMs = 3_000,
  ) {
    for (const sel of selectors) {
      try {
        const loc = this.page.locator(sel).first();
        if (await loc.isVisible({ timeout: timeoutMs / selectors.length })) {
          return loc;
        }
      } catch {}
    }
    return null;
  }

  // Click a Locator with a human-like mouse trajectory.
  private async humanClick(locator: Awaited<ReturnType<typeof this.firstVisible>>): Promise<void> {
    if (!locator) return;
    const box = await locator.boundingBox();
    if (!box) {
      await locator.click();
      return;
    }
    const x = box.x + box.width * 0.5 + randomBetween(-5, 5);
    const y = box.y + box.height * 0.5 + randomBetween(-5, 5);
    await this.page.mouse.move(x, y, { steps: Math.ceil(randomBetween(5, 12)) });
    await sleep(randomBetween(60, 160));
    await this.page.mouse.click(x, y);
  }

  // Type a string with randomised inter-character delays.
  private async typeHuman(text: string): Promise<void> {
    for (const ch of text) {
      await this.page.keyboard.type(ch);
      await sleep(randomBetween(60, 180));
    }
  }

  // Dismiss common overlays (cookie consent, welcome modals, etc.)
  async dismissPopups(): Promise<void> {
    const closers = [
      '[id*="cookie"] button:has-text("Accept")',
      '[id*="cookie"] button:has-text("OK")',
      'button[aria-label="Close"]',
      'button:has-text("Got it")',
      'button:has-text("Dismiss")',
      '[class*="modal"] [class*="close"]',
      '[class*="overlay"] [class*="close"]',
    ];
    for (const sel of closers) {
      try {
        const btn = this.page.locator(sel).first();
        if (await btn.isVisible({ timeout: 500 })) {
          await btn.click();
          await sleep(300);
        }
      } catch {}
    }
  }
}
