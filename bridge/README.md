# chess-bridge

Connects the compiled chess engine to chess.com and plays games automatically against computer bots.

## Prerequisites

- Node.js 18+ (you have 24)
- The compiled engine binary at `../engine/build/chess_engine`

## Setup

```bash
cd bridge
npm install
npx playwright install chromium
```

## Configuration

Edit `.env` (already populated):

| Variable | Default | Description |
|---|---|---|
| `CHESSDOTCOM_USERNAME` | samshiva11 | Your chess.com username |
| `CHESSDOTCOM_PASSWORD` | speterson1 | Your chess.com password |
| `ENGINE_PATH` | ../engine/build/chess_engine | Path to the engine binary (relative to bridge/) |
| `HEADLESS` | false | Set to `true` to run without a visible browser window |
| `BOT_NAME` | Martin | Name of the computer bot to play against |

## Running

```bash
npm start
```

A browser window will open (HEADLESS=false), log into chess.com, navigate to Play vs Computer, select Martin, and start playing. The engine thinks for 1 second per move. A random 0.5–2s human-like delay is added before each move.

Press **CTRL+C** to stop cleanly.

## How it works

```
index.ts  →  browser.ts  (Playwright)  →  chess.com DOM
          →  engine.ts   (child_process) →  UCI engine binary
          ←  utils.ts    (chess.js SAN→UCI conversion)
```

1. **Login** — navigates to chess.com/login, fills credentials, saves session cookies to `storage.json` for reuse.
2. **Start game** — navigates to /play/computer, finds the bot card by name, clicks Play.
3. **Game loop** — polls the DOM move list for new SAN moves, converts them to UCI via chess.js, pipes the full move history to the engine as `position startpos moves ...`, reads `bestmove`, adds a human delay, then clicks the from/to squares.
4. **Game over** — detects the game-over overlay, logs the result, waits 5–10s, starts a new game.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Bot "Martin" not found` | Open `/tmp/debug-bot-select.png`. Update `SEL.botCard` in `browser.ts` to match the current DOM. |
| `Chess board not found` | Open `/tmp/debug-no-board.png`. Update `SEL.board` in `browser.ts`. |
| Moves not executing | chess.com may have changed their square class format. Check that `squareToClass()` in `utils.ts` produces the right class (e.g. `square-54` for e4). Inspect the board DOM and update `SEL.square` if needed. |
| Login CAPTCHA | Disable `HEADLESS` (already false), run once manually and solve the CAPTCHA in the browser. Session will be saved to `storage.json`. |
| Move list not reading | chess.com uses web components with shadow DOM. The `readMoves()` function in `browser.ts` tries several selectors and pierces shadow roots via `page.evaluate`. Add the correct selector to the `sels` array there. |

## Notes

- This script **only plays against computer bots** via `/play/computer`.  It never challenges human players.
- chess.com's ToS prohibits automated play. Use at your own discretion and risk.
