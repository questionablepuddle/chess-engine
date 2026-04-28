import { chromium, Browser, BrowserContext, Page } from 'playwright';
import * as fs from 'fs';
import * as path from 'path';
import { squareToClass, sleep, humanDelay, randomBetween, log } from './utils';

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

  // Login page
  loginUsername: ['#username', 'input[name="username"]', 'input[autocomplete="username"]'],
  loginPassword: ['#password', 'input[name="password"]', 'input[type="password"]'],
  loginSubmit: ['button[type="submit"]', '.login-btn', 'button:has-text("Log In")', 'button:has-text("Sign In")'],

  // Bot selection on /play/computer
  botCard: (name: string) => [
    `[class*="bot-card"]:has-text("${name}")`,
    `[class*="bot-name"]:has-text("${name}")`,
    `[class*="card"]:has-text("${name}")`,
    `text="${name}"`,
  ],
  playButton: [
    'button[class*="play"]:visible',
    'button:has-text("Play"):visible',
    'button:has-text("Play Game"):visible',
    'button:has-text("Challenge"):visible',
    'button:has-text("Start"):visible',
  ],
};

const STORAGE_PATH = path.join(__dirname, '..', 'storage.json');

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
  // Initialisation
  // ---------------------------------------------------------------------------

  async init(): Promise<void> {
    const headless = process.env.HEADLESS !== 'false';
    this.browser = await chromium.launch({
      headless,
      args: [
        '--disable-blink-features=AutomationControlled',
        '--no-sandbox',
      ],
    });

    const ctxOpts = {
      viewport: { width: 1280, height: 900 },
      userAgent:
        'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' +
        '(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36',
      locale: 'en-US',
    };

    if (fs.existsSync(STORAGE_PATH)) {
      log('Browser', 'Loading saved session from storage.json');
      this.context = await this.browser.newContext({
        ...ctxOpts,
        storageState: STORAGE_PATH,
      });
    } else {
      this.context = await this.browser.newContext(ctxOpts);
    }

    this.page = await this.context.newPage();

    // Mask webdriver flag
    await this.page.addInitScript(() => {
      Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
    });

    log('Browser', 'Browser ready');
  }

  // ---------------------------------------------------------------------------
  // Login
  // ---------------------------------------------------------------------------

  async login(): Promise<void> {
    const username = process.env.CHESSDOTCOM_USERNAME!;
    const password = process.env.CHESSDOTCOM_PASSWORD!;

    // Always start fresh — delete stale session so we don't silently skip login
    if (fs.existsSync(STORAGE_PATH)) {
      fs.unlinkSync(STORAGE_PATH);
      log('Browser', 'Deleted stale storage.json');
    }

    log('Browser', `Logging in as ${username}…`);
    await this.page.goto('https://www.chess.com/login', { waitUntil: 'domcontentloaded' });
    await sleep(1500);
    await this.dismissPopups();

    // Wait for and fill username
    const userField = await this.firstVisible(SEL.loginUsername, 10_000);
    if (!userField) throw new Error('Cannot find username field on login page');
    await userField.click();
    await this.typeHuman(username);

    await sleep(300 + Math.random() * 300);

    // Fill password
    const passField = await this.firstVisible(SEL.loginPassword, 5_000);
    if (!passField) throw new Error('Cannot find password field on login page');
    await passField.click();
    await this.typeHuman(password);

    await sleep(300 + Math.random() * 300);

    // Submit
    const submit = await this.firstVisible(SEL.loginSubmit);
    if (!submit) throw new Error('Cannot find login submit button');
    await submit.click();

    // If still on /login after 3s, assume CAPTCHA — screenshot and wait for manual solve
    await sleep(3_000);
    if (this.page.url().includes('/login')) {
      await this.page.screenshot({ path: '/tmp/debug-captcha.png' });
      log('Browser', 'Possible CAPTCHA — screenshot at /tmp/debug-captcha.png');
      log('Browser', 'Waiting 30s for manual CAPTCHA solve…');
      await sleep(30_000);
    }

    // Wait for redirect away from login page (up to 15s)
    await this.page.waitForFunction(
      () => !window.location.href.includes('/login'),
      { timeout: 15_000 },
    ).catch(() => {
      throw new Error('Login failed — still on /login after 15s. Check credentials or solve CAPTCHA manually.');
    });

    // Persist session
    await this.context.storageState({ path: STORAGE_PATH });
    log('Browser', 'Login successful, session saved');

    // Verify: "Sign Up" / "Log In" button must be gone
    const signUpStillVisible = await this.page
      .locator('a:has-text("Sign Up"), button:has-text("Sign Up"), a:has-text("Log In")')
      .first()
      .isVisible({ timeout: 2_000 })
      .catch(() => false);
    if (signUpStillVisible) {
      throw new Error('Login verification failed — guest buttons still visible after login');
    }
    log('Browser', 'Login verified');
  }

  // ---------------------------------------------------------------------------
  // Navigate to computer play page and start game vs selected bot
  // ---------------------------------------------------------------------------

  async startGameVsBot(): Promise<void> {
    const botName = process.env.BOT_NAME ?? 'Martin';

    await this.page.goto('https://www.chess.com/play/computer', {
      waitUntil: 'domcontentloaded',
    });
    await sleep(2000);
    await this.dismissPopups();

    log('Browser', `Selecting bot: ${botName}`);

    // --- Expand the "Beginner" category so bot cards become visible ---
    // chess.com groups bots: The Circus / Beginner / Intermediate / Advanced / Master / Adaptive
    const categoryLabel = 'Beginner';
    const categorySelectors = [
      `[class*="bot-group"]:has-text("${categoryLabel}")`,
      `[class*="category"]:has-text("${categoryLabel}")`,
      `[class*="section-header"]:has-text("${categoryLabel}")`,
      `[class*="group-header"]:has-text("${categoryLabel}")`,
      `[class*="accordion"]:has-text("${categoryLabel}")`,
      `button:has-text("${categoryLabel}")`,
      `h2:has-text("${categoryLabel}")`,
      `h3:has-text("${categoryLabel}")`,
      `[class*="header"]:has-text("${categoryLabel}")`,
      `[class*="title"]:has-text("${categoryLabel}")`,
    ];

    for (const sel of categorySelectors) {
      try {
        const el = this.page.locator(sel).first();
        if (await el.isVisible({ timeout: 1_500 })) {
          await this.humanClick(el);
          log('Browser', `Clicked category header: ${sel}`);
          await sleep(800);
          break;
        }
      } catch {}
    }

    // Scroll down in case the category is below the fold
    await this.page.evaluate(() => window.scrollBy(0, 300));
    await sleep(400);

    // --- Find the bot card ---
    const botEl = await this.firstVisible(SEL.botCard(botName), 8_000);

    if (!botEl) {
      await this.page.screenshot({ path: '/tmp/debug-bot-select.png' });
      throw new Error(
        `Bot "${botName}" not found on /play/computer. ` +
        `Screenshot saved to /tmp/debug-bot-select.png. ` +
        `Update SEL.botCard or categoryLabel if the DOM changed.`,
      );
    }

    await this.humanClick(botEl);
    await sleep(1_000);

    // Screenshot to confirm bot is selected (name should appear in right-panel header)
    await this.page.screenshot({ path: '/tmp/debug-bot-selected.png' });
    log('Browser', `Bot clicked — verify selection at /tmp/debug-bot-selected.png`);

    // --- Click the green Play button ---
    const playBtn = await this.firstVisible(SEL.playButton, 6_000);
    if (playBtn) {
      await this.humanClick(playBtn);
      log('Browser', 'Clicked Play button');
    } else {
      log('Browser', 'No Play button found — game may start automatically');
    }

    // --- Wait for the board to become interactive ---
    await this.waitForBoard();

    // --- Detect our colour ---
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
  async executeMove(uciMove: string, prevMoveCount: number): Promise<void> {
    const from = uciMove.slice(0, 2);
    const to = uciMove.slice(2, 4);
    const promo = uciMove.length === 5 ? uciMove[4] : undefined;

    log('Browser', `Executing move: ${uciMove}`);

    await this.clickSquare(from);
    await sleep(80 + randomBetween(40, 120));
    await this.clickSquare(to);

    // Handle promotion dialog
    if (promo) {
      await this.handlePromotion(promo);
    }

    // Wait up to 3s for the move to register (DOM move count increases)
    const deadline = Date.now() + 3_000;
    while (Date.now() < deadline) {
      const current = (await this.readMoves()).length;
      if (current > prevMoveCount) break;
      await sleep(100);
    }
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
    while (Date.now() < deadline) {
      if (await this.isGameOver()) throw new Error('GAME_OVER');
      const moves = await this.readMoves();
      if (moves.length >= expectedMoveCount) return moves.length;
      await sleep(200);
    }
    throw new Error(`waitForOurTurn timed out after ${timeoutMs}ms`);
  }

  // ---------------------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------------------

  async close(): Promise<void> {
    await this.browser?.close();
  }

  // ---------------------------------------------------------------------------
  // Private helpers
  // ---------------------------------------------------------------------------

  private async waitForBoard(): Promise<void> {
    log('Browser', 'Waiting for board…');
    for (const sel of SEL.board) {
      try {
        await this.page.waitForSelector(sel, { timeout: 15_000 });
        log('Browser', `Board found: ${sel}`);
        await sleep(500); // let it settle
        return;
      } catch {}
    }
    await this.page.screenshot({ path: '/tmp/debug-no-board.png' });
    throw new Error('Chess board not found after 15s — screenshot at /tmp/debug-no-board.png');
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
    const cls = squareToClass(sq); // e.g. "square-54"

    for (const sel of SEL.square(cls)) {
      try {
        const loc = this.page.locator(sel).first();
        const count = await loc.count();
        if (count === 0) continue;

        const box = await loc.boundingBox();
        if (!box) continue;

        // Move mouse to a slightly random position within the square
        const x = box.x + box.width * 0.5 + randomBetween(-3, 3);
        const y = box.y + box.height * 0.5 + randomBetween(-3, 3);
        await this.page.mouse.move(x, y, { steps: Math.ceil(randomBetween(3, 8)) });
        await sleep(40 + randomBetween(20, 60));
        await this.page.mouse.click(x, y);
        return;
      } catch {}
    }

    // Last resort: use coordinates from board bounding box + calculated offset
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
