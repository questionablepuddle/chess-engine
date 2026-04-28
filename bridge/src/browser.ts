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

  // Detected at game start
  ourColor: 'white' | 'black' = 'white';

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
    log('Browser', 'Navigating to chess.com/play/computer…');
    await this.page.goto('https://www.chess.com/play/computer', {
      waitUntil: 'domcontentloaded',
      timeout: 30_000,
    });
    await this.page.waitForLoadState('networkidle', { timeout: 15_000 }).catch(() => {
      log('Browser', 'networkidle timeout — continuing anyway');
    });
    await sleep(2_000);
    await this.dismissPopups();

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

  // Returns all SAN move strings currently shown in the move list (both sides).
  async readMoves(): Promise<string[]> {
    // Use page.evaluate to pierce shadow DOM of web components
    const moves = await this.page.evaluate(() => {
      function collectFrom(root: Document | ShadowRoot): string[] {
        const sels = [
          'wc-simple-move-list .node',
          '.vertical-move-list .node',
          '.moves .move-node',
          '[class*="move-list"] [class*="node"]',
        ];
        for (const sel of sels) {
          try {
            const els = Array.from(root.querySelectorAll(sel));
            const texts = els
              .map(e => e.textContent?.trim() ?? '')
              .filter(t => t.length > 0 && !/^\d+\./.test(t)); // skip "1.", "2.", etc.
            if (texts.length) return texts;
          } catch {}
        }

        // Try shadow roots of known web components
        for (const wc of ['wc-simple-move-list', 'vertical-move-list']) {
          const el = root.querySelector(wc);
          if (el instanceof HTMLElement) {
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

      const moves = await this.readMoves();
      log('Browser', `Move list (${moves.length}): ${moves.slice(-4).join(' ') || '(empty)'}`);

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

  private async clickSquare(sq: string): Promise<void> {
    // chess.com class: square-{file}{rank}, a=1..h=8, rank 1-8
    const file = sq.charCodeAt(0) - 'a'.charCodeAt(0) + 1;
    const rank = parseInt(sq[1], 10);
    const squareClass = `.square-${file}${rank}`;

    log('Browser', `Clicking ${sq} → ${squareClass}`);

    try {
      const loc = this.page.locator(squareClass).first();
      const box = await loc.boundingBox({ timeout: 3_000 });
      if (box) {
        const x = box.x + box.width * 0.5 + randomBetween(-3, 3);
        const y = box.y + box.height * 0.5 + randomBetween(-3, 3);
        await this.page.mouse.move(x, y, { steps: Math.ceil(randomBetween(3, 8)) });
        await sleep(40 + randomBetween(20, 60));
        await this.page.mouse.click(x, y);
        return;
      }
    } catch (e) {
      log('Browser', `${squareClass} not found via CSS class — falling back to coordinates: ${e}`);
    }

    // Fallback: calculate from board bounding box
    await this.clickSquareByCoordinates(sq);
  }

  // Fallback: calculate the square center from the board's bounding box.
  private async clickSquareByCoordinates(sq: string): Promise<void> {
    const boardEl = await this.page.$(SEL.board.join(', '));
    if (!boardEl) throw new Error(`Cannot find board to click square ${sq}`);

    const box = await boardEl.boundingBox();
    if (!box) throw new Error(`Board has no bounding box`);

    const file = sq.charCodeAt(0) - 'a'.charCodeAt(0); // 0-7
    const rank = parseInt(sq[1], 10) - 1; // 0-7

    // If board is flipped (black perspective): invert both axes
    const flipped = this.ourColor === 'black';
    const col = flipped ? 7 - file : file;
    const row = flipped ? rank : 7 - rank;

    const sqSize = box.width / 8;
    const x = box.x + (col + 0.5) * sqSize + randomBetween(-3, 3);
    const y = box.y + (row + 0.5) * sqSize + randomBetween(-3, 3);

    await this.page.mouse.move(x, y, { steps: 5 });
    await sleep(50);
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
