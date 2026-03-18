# OpenChess - Copilot Instructions

## Project Overview
ESP32 Arduino smart chessboard: detects piece movements via hall-effect sensors + shift register, provides LED feedback via WS2812B strip, and communicates with Stockfish API / Lichess API over WiFi. Built with PlatformIO (`esp32dev` board, Arduino framework).

## Architecture

### Class Hierarchy & Inheritance
`ChessGame` (abstract) → `ChessMoves` (human v human, minimal override of `begin()`/`update()`).
`ChessGame` → `ChessBot` (v Stockfish, adds `makeBotMove()`) → `ChessLichess` (online play, adds polling + `sendMoveToLichess()`).
Note: `ChessLichess` inherits from `ChessBot` (not `ChessMoves`), reusing `waitForRemoteMoveCompletion()`. It passes a dummy `BotConfig` and `nullptr` for `MoveHistory` to `ChessBot`'s constructor — Lichess games are not saved locally.

`BoardDriver` and `ChessEngine` are shared via pointer injection — never duplicated. `SensorTest` is standalone (not a `ChessGame` subclass).

### Key Components
- **`BoardDriver`** (`board_driver.h/.cpp`) — hardware abstraction: LED strip (NeoPixel), sensor grid (shift register column scan + row GPIO reads), calibration (NVS-persisted), and async animation queue (FreeRTOS task + queue).
- **`ChessEngine`** (`chess_engine.h/.cpp`) — pure chess logic: move generation, validation, check/checkmate/stalemate, castling rights, en passant, 50-move rule, threefold repetition (Zobrist hashing). No hardware dependencies.
- **`WiFiManagerESP32`** (`wifi_manager_esp32.h/.cpp`) — async web server (`ESPAsyncWebServer`), REST API, WiFi management (STA with AP fallback), NVS persistence.
- **`MoveHistory`** (`move_history.h/.cpp`) — game save/load/resume system using compact binary format on LittleFS. `friend class` of `ChessGame` for access to `applyMove()`/`advanceTurn()` during replay.
- **`OtaUpdater`** (`ota_updater.h/.cpp`) — checks GitHub releases for updates, applies firmware (`.bin`) or web assets (`.tar`) OTA. Preserves `/games/` directory during web asset updates.
- **`ChessUtils`** — static helpers: FEN ↔ board conversion, material evaluation, NVS init.

### Main Loop State Machine (`main.cpp`)
`GameMode` enum: `MODE_SELECTION=0`, `MODE_CHESS_MOVES=1`, `MODE_BOT=2`, `MODE_LICHESS=3`, `MODE_SENSOR_TEST=4`.
- `setup()`: NVS init → LittleFS → MoveHistory → BoardDriver hardware → WiFi → calibration check → live game resume check
- `loop()`: deferred WiFi reconnect → check pending web actions (board edit, resign, draw) → mode selection or game `update()` → 40ms delay
- Game mode objects are heap-allocated (`new`/`delete` on mode switch). Shared objects (`boardDriver`, `chessEngine`, `moveHistory`) are stack-allocated globals.

### Coordinate System
Board arrays use `[row][col]` where **row 0 = rank 8** (black's back rank), **col 0 = file a**. Moves are `int[2]` arrays (row, col) in fixed-size `int moves[28][2]` arrays — no Move struct.

## Build & Flash

### Prerequisites
- VS Code + PlatformIO IDE extension
- For web asset minification: `npm install -g html-minifier-terser clean-css-cli terser`

### Build Pipeline
PlatformIO runs pre-build scripts defined in `platformio.ini`:
1. `src/web/build/minify.py` — minifies HTML/CSS/JS from `src/web/` (skips gracefully if npm tools absent)
2. `src/web/build/prepare_littlefs.py` — gzip-compresses assets → `data/` for LittleFS, then deletes minified intermediates
3. `src/web/build/upload_fs.py` — on `pio run -t upload`, hashes `data/` and only uploads filesystem image when content changed

The `data/` directory is **committed to git** so the project builds without minification tools. Edit source HTML/CSS/JS in `src/web/`, not `data/`.

### Commands
PlatformIO CLI (`pio`) is not on PATH by default. Use the full path:
- **Windows**: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`
- **Linux**: `~/.platformio/penv/bin/pio`

### CI/CD (`release.yml`)
On `v*` tags: injects tag into `version.h` via `sed`, builds firmware + LittleFS, creates `web_assets.tar` from `data/`, publishes GitHub Release. `pages.yml` deploys the web flasher from `docs/` to GitHub Pages.

## Patterns & Conventions

### FreeRTOS Concurrency
- **LED mutex**: `acquireLEDs()`/`releaseLEDs()` for multi-step LED updates. Single animation calls acquire automatically.
- **Animation queue**: `AnimationJob` struct with `AnimationType` enum (`CAPTURE`, `PROMOTION`, `BLINK`, `WAITING`, `THINKING`, `FIREWORK`, `FLASH`). Worker task processes jobs from `QueueHandle_t`. Cancellable animations return `std::atomic<bool>*` stop flag — set `store(true)` to cancel; the worker owns and deletes the flag.
- **Deferred operations**: WiFi reconnection deferred to main loop via `checkPendingWiFi()` to avoid blocking `async_tcp` task (ESP32 watchdog). Hardware config reboot and OTA apply use ad-hoc `xTaskCreate` for deferred execution after HTTP response.

### Color Semantics (`LedColors` namespace in `led_colors.h`)
Fixed meanings: `Cyan`=piece origin, `White`=valid move, `Red`=capture/error, `Purple`=en passant, `Green`=confirmation, `Yellow`=check/promotion, `Blue`=bot thinking, `Off`=clear. Use these consistently.

### Game Save/Resume (`MoveHistory`)
- **Binary format**: 16-byte packed `GameHeader` + 2-byte encoded moves (6 bits from + 6 bits to + 4 bits promotion). FEN snapshots stored separately during recording, merged on `finishGame()`.
- **Live files**: `/games/live.bin` + `/games/live_fen.bin`. Completed: `/games/game_NN.bin` (1-based, zero-padded `game_%02d.bin`).
- **Resume**: At boot, finds last FEN marker in live game, restores board state, replays remaining moves with `replaying=true` flag (suppresses LEDs and physical waits).
- **Storage limits**: `MAX_GAMES=50`, `MAX_USAGE_PERCENT=0.80` — oldest games deleted first.
- **`quietExists()`**: Uses POSIX `stat()` with `"/littlefs"` prefix instead of `LittleFS.exists()` to avoid noisy VFS log output.

### Web UI & REST API
Three pages in `src/web/`: `index.html` (settings), `game.html` (mode selection), `board.html` (live board with chessboard.js/chess.js). Communication via `fetch()` polling `/board-update` for FEN. Key endpoints:

| Endpoint | Purpose |
|----------|---------|
| `GET/POST /board-update` | FEN + eval polling / board edit |
| `POST /promotion` | Piece selection (`?piece=q/r/b/n`) |
| `POST /resign`, `/draw` | Game end actions |
| `POST /gameselect` | Mode + config selection |
| `GET/POST /wifi` | WiFi credentials |
| `GET/POST /lichess` | Token management |
| `GET/POST /board-settings` | Brightness, dim multiplier |
| `GET/DELETE /games` | Game history (binary format, parsed client-side) |
| `POST /ota/upload/firmware` | Raw binary body upload |
| `POST /ota/upload/web` | TAR upload for web assets |
| `GET/POST /hardware-config` | GPIO pin configuration |

### Web Assets Conventions
- Files named `*.nogz.*` (e.g., `capture.nogz.mp3`) skip gzip in the build pipeline — for binary files.
- `/sounds/` served with `setTryGzipFirst(false)`. `/pieces/` served with aggressive cache headers. Other static files auto-detect gzip.

### NVS Persistence
Namespaces: `"wifiCreds"` (ssid, pass), `"lichess"` (token), `"ota"` (autoUpdate), plus `BoardDriver` namespaces for LED settings, calibration, and hardware config. Always call `ChessUtils::ensureNvsInitialized()` before first use.

### External APIs
- **Stockfish**: HTTPS to `stockfish.online:443/api/s/v2.php`. Depth presets: Easy=5, Medium=8, Hard=11, Expert=15. Configurable `maxRetries`.
- **Lichess**: HTTPS to `lichess.org:443`, polling-based (`POLL_INTERVAL_MS=500`). Token in NVS. Move retries: 3 attempts with 500ms delay.

### Error Handling
`Serial.println` for diagnostics. No exceptions. WiFi failures → AP fallback (`SSID "OpenChess"`, password `"chess123"`). OTA failures → flash red LEDs. Version `"dev"` always considers remote updates as newer.

### Pin Configuration
GPIO pins defined in `board_driver.h` with defaults, runtime-configurable via `HardwareConfig` struct persisted in NVS (editable from web UI). Calibration maps physical pin order to logical coordinates — **pin assignment order doesn't matter**.
