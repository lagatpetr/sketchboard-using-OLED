#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------- WiFi hotspot (change these) ----------
const char* AP_SSID = "SketchBoard";
const char* AP_PASS = "sketch1234";   // must be at least 8 characters

// ---------- Screen chip ----------
// true = SH1106 (1.3" screens), false = SSD1306 (0.96")
#define USE_SH1106 true

// U8G2_R0 = normal, U8G2_R2 = upside down
#define SCREEN_ROTATION U8G2_R0

// ---------- Startup animation ----------
#define ANIM_DELAY_MS 40      // lower = smoother and faster
#define BALL_SPEED    1.0f    // 0.5 = slow, 1.0 = normal, 1.5 = fast

// ---------- Dotted trail ----------
#define TRAIL_LEN     12      // how many dots are remembered (longer = longer tail)
#define TRAIL_EVERY   2       // drop a dot every N frames (bigger = more spaced out dots)
#define TRAIL_BIG     3       // newest dots are 2x2 squares
#define TRAIL_MEDIUM  7       // then single pixels, then every other dot, then gone

// The invisible walls the smiley bounces off (no frame is drawn)
#define BALL_MIN_X 10
#define BALL_MAX_X 117
#define BALL_MIN_Y 26
#define BALL_MAX_Y 53

#if USE_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(SCREEN_ROTATION, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(SCREEN_ROTATION, U8X8_PIN_NONE);
#endif

WebServer server(80);

uint8_t  frame[1024];          // 128 x 64 pixels, 1 bit each
bool     frameDirty    = false;
bool     frameInverted = false;
bool     gotFrame      = false;   // true after the first drawing arrives
String   ipText        = "192.168.4.1";
bool     apOk          = false;
uint16_t animTick      = 0;
uint32_t lastAnim      = 0;

// Bouncing smiley state
float   bx = 30, by = 38;          // position
float   bvx = 2.2f, bvy = 1.5f;    // speed
uint8_t squashTimer  = 0;          // frames left in the "squash" after a hit
bool    squashHoriz  = false;      // hit a left/right wall
bool    squashVert   = false;      // hit the top/bottom wall

// Trail memory (ring buffer of past positions)
int16_t trailX[TRAIL_LEN];
int16_t trailY[TRAIL_LEN];
uint8_t trailHead  = 0;            // where the next dot will be stored
uint8_t trailCount = 0;            // how many dots are stored so far
uint8_t trailSkip  = 0;            // counts frames between dots

// ---------- The web page ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>Sketch Board</title>
<style>
  /* Change --boardMax to make the board wider or narrower on a laptop */
  :root{--bg:#10151c;--panel:#1b232e;--line:#2e3a4a;--text:#e8eef5;--accent:#4cc2ff;--boardMax:1600px;}
  *{box-sizing:border-box;touch-action:manipulation;}
  body{margin:0;padding:6px 6px 30px;background:var(--bg);color:var(--text);
       font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
       display:flex;justify-content:center;}
  #app{width:100%;max-width:var(--boardMax);}
  header{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;}
  h1{font-size:1.2rem;margin:0;}
  #status{font-size:.85rem;color:#8aa0b5;}
  canvas{display:block;width:100%;aspect-ratio:2/1;background:#000;
         border:2px solid var(--line);border-radius:10px;
         image-rendering:pixelated;touch-action:none;cursor:crosshair;}
  #controls{margin-top:20px;}
  .panel{background:#151c26;border:1px solid var(--line);border-radius:12px;
         padding:12px;margin-top:12px;}
  .panel h2{font-size:.78rem;letter-spacing:.08em;text-transform:uppercase;
            color:#8aa0b5;margin:0 0 10px;}
  .grid{display:grid;gap:8px;}
  .tools{grid-template-columns:repeat(4,1fr);}
  .two{grid-template-columns:1fr 1fr;}
  .textrow{grid-template-columns:2fr 1fr;}
  @media (min-width:700px){.tools{grid-template-columns:repeat(7,1fr);}}
  button,select,input[type=text]{width:100%;background:var(--panel);color:var(--text);
         border:1px solid var(--line);border-radius:8px;padding:13px 8px;font-size:1rem;}
  button.active{background:var(--accent);color:#001018;border-color:var(--accent);font-weight:600;}
  label.box{display:flex;align-items:center;gap:8px;background:var(--panel);
            border:1px solid var(--line);border-radius:8px;padding:8px 12px;font-size:1rem;}
  label.box select{flex:1;border:none;padding:6px 4px;}
  input[type=checkbox]{width:22px;height:22px;}
</style>
</head>
<body>
<div id="app">
  <header>
    <h1>Sketch Board</h1>
    <span id="status">Ready</span>
  </header>

  <canvas id="cv"></canvas>

  <div id="controls">
    <div class="panel">
      <h2>Tools</h2>
      <div class="grid tools" id="tools">
        <button data-tool="pen" class="active">Pen</button>
        <button data-tool="eraser">Eraser</button>
        <button data-tool="line">Line</button>
        <button data-tool="rect">Rectangle</button>
        <button data-tool="circle">Circle</button>
        <button data-tool="triangle">Triangle</button>
        <button data-tool="text">Text</button>
      </div>
    </div>

    <div class="panel">
      <h2>Actions</h2>
      <div class="grid two">
        <button id="undo">Undo</button>
        <button id="clear">Clear</button>
      </div>
    </div>

    <div class="panel">
      <h2>Options</h2>
      <div class="grid two">
        <label class="box">Size
          <select id="size">
            <option value="1">1 px</option>
            <option value="2">2 px</option>
            <option value="3">3 px</option>
            <option value="4">4 px</option>
          </select>
        </label>
        <label class="box"><input type="checkbox" id="fill"> Filled shapes</label>
        <label class="box"><input type="checkbox" id="inv"> Invert screen</label>
      </div>
    </div>

    <div class="panel">
      <h2>Text</h2>
      <div class="grid textrow">
        <input type="text" id="txt" placeholder="Type your text here" maxlength="40">
        <select id="txtSize">
          <option value="8">Small</option>
          <option value="12" selected>Medium</option>
          <option value="18">Large</option>
        </select>
      </div>
    </div>
  </div>
</div>

<script>
const W = 128, H = 64;
const cv  = document.getElementById('cv');
const ctx = cv.getContext('2d');
cv.width = W; cv.height = H;
const img = ctx.createImageData(W, H);

let bits = new Uint8Array(W * H);
let history = [];
let tool = 'pen', size = 1, filled = false, inverted = false;

const statusEl = document.getElementById('status');
function setStatus(t) { statusEl.textContent = t; }

// ---------- Show the bitmap on the page ----------
function render() {
  const d = img.data;
  for (let i = 0; i < W * H; i++) {
    const on = inverted ? !bits[i] : bits[i];
    const v = on ? 255 : 0;
    d[i*4] = v; d[i*4+1] = v; d[i*4+2] = v; d[i*4+3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

// ---------- Drawing helpers ----------
function setpx(b, x, y, v) {
  if (x >= 0 && x < W && y >= 0 && y < H) b[y * W + x] = v;
}
function plot(b, x, y, s, v) {
  const h = Math.floor(s / 2);
  for (let dy = 0; dy < s; dy++)
    for (let dx = 0; dx < s; dx++)
      setpx(b, x - h + dx, y - h + dy, v);
}
function line(b, x0, y0, x1, y1, s, v) {
  let dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  let dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  let err = dx + dy;
  while (true) {
    plot(b, x0, y0, s, v);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
function rect(b, x0, y0, x1, y1, s, fill) {
  const xa = Math.min(x0, x1), xb = Math.max(x0, x1);
  const ya = Math.min(y0, y1), yb = Math.max(y0, y1);
  if (fill) {
    for (let y = ya; y <= yb; y++)
      for (let x = xa; x <= xb; x++) setpx(b, x, y, 1);
  } else {
    line(b, xa, ya, xb, ya, s, 1);
    line(b, xb, ya, xb, yb, s, 1);
    line(b, xb, yb, xa, yb, s, 1);
    line(b, xa, yb, xa, ya, s, 1);
  }
}
function circle(b, cx, cy, r, s, fill) {
  if (fill) {
    for (let y = -r; y <= r; y++) {
      const hw = Math.floor(Math.sqrt(r * r - y * y) + 0.5);
      for (let x = -hw; x <= hw; x++) setpx(b, cx + x, cy + y, 1);
    }
    return;
  }
  let x = r, y = 0, d = 1 - x;
  while (y <= x) {
    plot(b, cx + x, cy + y, s, 1); plot(b, cx - x, cy + y, s, 1);
    plot(b, cx + x, cy - y, s, 1); plot(b, cx - x, cy - y, s, 1);
    plot(b, cx + y, cy + x, s, 1); plot(b, cx - y, cy + x, s, 1);
    plot(b, cx + y, cy - x, s, 1); plot(b, cx - y, cy - x, s, 1);
    y++;
    if (d <= 0) d += 2 * y + 1;
    else { x--; d += 2 * (y - x) + 1; }
  }
}
function triangle(b, x0, y0, x1, y1, s, fill) {
  const xa = Math.min(x0, x1), xb = Math.max(x0, x1);
  const ya = Math.min(y0, y1), yb = Math.max(y0, y1);
  const ax = Math.round((xa + xb) / 2);        // apex at top centre
  if (fill) {
    const hgt = yb - ya;
    for (let y = ya; y <= yb; y++) {
      const t = hgt === 0 ? 1 : (y - ya) / hgt;
      const l = Math.round(ax + (xa - ax) * t);
      const r = Math.round(ax + (xb - ax) * t);
      for (let x = l; x <= r; x++) setpx(b, x, y, 1);
    }
  } else {
    line(b, ax, ya, xb, yb, s, 1);
    line(b, xb, yb, xa, yb, s, 1);
    line(b, xa, yb, ax, ya, s, 1);
  }
}

// Text is drawn on a hidden canvas, then turned into black/white pixels
const sc = document.createElement('canvas');
sc.width = W; sc.height = H;
const sctx = sc.getContext('2d');
function drawText(b, str, x, y, px) {
  sctx.fillStyle = '#000';
  sctx.fillRect(0, 0, W, H);
  sctx.fillStyle = '#fff';
  sctx.font = 'bold ' + px + 'px sans-serif';
  sctx.textBaseline = 'top';
  sctx.fillText(str, x, y);
  const d = sctx.getImageData(0, 0, W, H).data;
  for (let i = 0; i < W * H; i++) if (d[i * 4] > 110) b[i] = 1;
}

// ---------- Sending to the ESP32 ----------
function toHex() {
  const out = [];
  for (let y = 0; y < H; y++) {
    for (let bx = 0; bx < 16; bx++) {
      let v = 0;
      for (let k = 0; k < 8; k++) if (bits[y * W + bx * 8 + k]) v |= (1 << k);
      out.push((v < 16 ? '0' : '') + v.toString(16));
    }
  }
  return out.join('');
}
let inFlight = false, pending = false;
function queueSend() {
  pending = true;
  if (!inFlight) sendFrame();
}
async function sendFrame() {
  inFlight = true;
  pending = false;
  try {
    const r = await fetch('/frame?inv=' + (inverted ? 1 : 0), {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: toHex()
    });
    setStatus(r.ok ? 'Connected' : 'Error');
  } catch (e) {
    setStatus('Not connected');
  }
  inFlight = false;
  if (pending) sendFrame();
}

// ---------- History ----------
function pushHistory() {
  history.push(bits.slice());
  if (history.length > 40) history.shift();
}

// ---------- Mouse / touch ----------
function pos(e) {
  const r = cv.getBoundingClientRect();
  const x = Math.min(W - 1, Math.max(0, Math.floor((e.clientX - r.left) / r.width * W)));
  const y = Math.min(H - 1, Math.max(0, Math.floor((e.clientY - r.top) / r.height * H)));
  return [x, y];
}
let drawing = false, sx = 0, sy = 0, lx = 0, ly = 0, base = null;

function stroke(x0, y0, x1, y1) {
  if (tool === 'pen') line(bits, x0, y0, x1, y1, size, 1);
  else line(bits, x0, y0, x1, y1, size * 3, 0);
}
function shapePreview(x, y) {
  bits.set(base);
  if (tool === 'line') line(bits, sx, sy, x, y, size, 1);
  else if (tool === 'rect') rect(bits, sx, sy, x, y, size, filled);
  else if (tool === 'circle') {
    const r = Math.round(Math.hypot(x - sx, y - sy));
    circle(bits, sx, sy, r, size, filled);
  }
  else if (tool === 'triangle') triangle(bits, sx, sy, x, y, size, filled);
}
function placeText(x, y) {
  const t = document.getElementById('txt').value;
  if (!t) { setStatus('Type some text first'); return; }
  pushHistory();
  drawText(bits, t, x, y, parseInt(document.getElementById('txtSize').value));
  render();
  queueSend();
}

cv.addEventListener('pointerdown', e => {
  e.preventDefault();
  cv.setPointerCapture(e.pointerId);
  const [x, y] = pos(e);
  if (tool === 'text') { placeText(x, y); return; }
  pushHistory();
  drawing = true;
  sx = lx = x; sy = ly = y;
  if (tool === 'pen' || tool === 'eraser') stroke(x, y, x, y);
  else base = bits.slice();
  render();
  queueSend();
});
cv.addEventListener('pointermove', e => {
  if (!drawing) return;
  const [x, y] = pos(e);
  if (tool === 'pen' || tool === 'eraser') { stroke(lx, ly, x, y); lx = x; ly = y; }
  else shapePreview(x, y);
  render();
  queueSend();
});
function endDraw() { drawing = false; base = null; }
cv.addEventListener('pointerup', endDraw);
cv.addEventListener('pointercancel', endDraw);

// ---------- Buttons ----------
document.querySelectorAll('[data-tool]').forEach(btn => {
  btn.addEventListener('click', () => {
    tool = btn.dataset.tool;
    document.querySelectorAll('[data-tool]').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    setStatus(tool === 'text' ? 'Tap the board to place text' : 'Ready');
  });
});
document.getElementById('size').addEventListener('change', e => { size = parseInt(e.target.value); });
document.getElementById('fill').addEventListener('change', e => { filled = e.target.checked; });
document.getElementById('inv').addEventListener('change', e => {
  inverted = e.target.checked;
  render();
  queueSend();
});
document.getElementById('undo').addEventListener('click', () => {
  if (history.length) {
    bits = history.pop();
    render();
    queueSend();
  }
});
document.getElementById('clear').addEventListener('click', () => {
  pushHistory();
  bits.fill(0);
  render();
  queueSend();
});

render();
</script>
</body>
</html>
)rawliteral";

// ---------- OLED: startup animation ----------
// rx / ry = half width / half height of the face (9 and 9 = normal round face)
void drawSmiley(int cx, int cy, int rx, int ry, bool happy) {
  u8g2.drawEllipse(cx, cy, rx, ry);                 // face

  int ex = (rx * 4) / 9;                            // eye spacing follows the face width
  int ey = (ry * 3) / 9;                            // eye height follows the face height
  if (ey < 2) ey = 2;

  if (happy) {                                      // squinting happy eyes
    u8g2.drawHLine(cx - ex - 1, cy - ey, 3);
    u8g2.drawHLine(cx + ex - 1, cy - ey, 3);
  } else {                                          // open eyes
    u8g2.drawDisc(cx - ex, cy - ey, 1);
    u8g2.drawDisc(cx + ex, cy - ey, 1);
  }

  // smile (lower half of a small circle)
  int mr = ((rx < ry ? rx : ry) * 5) / 9;
  u8g2.drawCircle(cx, cy - 1, mr, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
}

float randSpeedX() { return random(18, 28) / 10.0f; }   // 1.8 to 2.7
float randSpeedY() { return random(12, 21) / 10.0f; }   // 1.2 to 2.0

// Remember the smiley's position every few frames
void recordTrail() {
  trailSkip++;
  if (trailSkip < TRAIL_EVERY) return;
  trailSkip = 0;

  trailX[trailHead] = (int)(bx + 0.5f);
  trailY[trailHead] = (int)(by + 0.5f);
  trailHead = (trailHead + 1) % TRAIL_LEN;
  if (trailCount < TRAIL_LEN) trailCount++;
}

// Draw the dotted trail: newest dots are biggest, oldest fade away
void drawTrail() {
  int fx = (int)(bx + 0.5f);
  int fy = (int)(by + 0.5f);

  for (uint8_t age = 0; age < trailCount; age++) {
    uint8_t idx = (trailHead + TRAIL_LEN - 1 - age) % TRAIL_LEN;   // age 0 = newest
    int x = trailX[idx];
    int y = trailY[idx];

    // don't draw dots hidden inside the face
    int dx = x - fx, dy = y - fy;
    if (dx * dx + dy * dy < 13 * 13) continue;

    if (age < TRAIL_BIG) {
      u8g2.drawBox(x - 1, y - 1, 2, 2);            // strong dot
    } else if (age < TRAIL_MEDIUM) {
      u8g2.drawPixel(x, y);                        // normal dot
    } else if (age % 2 == 0) {
      u8g2.drawPixel(x, y);                        // sparse, fading dot
    }
  }
}

void drawStartupFrame() {
  u8g2.clearBuffer();

  // Title, centred
  u8g2.setFont(u8g2_font_helvB10_tr);
  const char* title = "Sketch your idea";
  int w = u8g2.getStrWidth(title);
  if (w > 126) {                                    // use a smaller font if too wide
    u8g2.setFont(u8g2_font_helvB08_tr);
    w = u8g2.getStrWidth(title);
  }
  u8g2.drawStr((128 - w) / 2, 11, title);

  // Move the smiley and bounce it off the invisible walls
  bx += bvx * BALL_SPEED;
  by += bvy * BALL_SPEED;
  bool hitX = false, hitY = false;

  if (bx < BALL_MIN_X)      { bx = BALL_MIN_X; bvx =  randSpeedX(); hitX = true; }
  else if (bx > BALL_MAX_X) { bx = BALL_MAX_X; bvx = -randSpeedX(); hitX = true; }
  if (by < BALL_MIN_Y)      { by = BALL_MIN_Y; bvy =  randSpeedY(); hitY = true; }
  else if (by > BALL_MAX_Y) { by = BALL_MAX_Y; bvy = -randSpeedY(); hitY = true; }

  if (hitX || hitY) {
    squashTimer = 4;
    squashHoriz = hitX;
    squashVert  = hitY;
  }

  // Dotted trail behind the smiley
  recordTrail();
  drawTrail();

  // Squash the face for a few frames after a hit
  int rx = 9, ry = 9;
  bool squashing = squashTimer > 0;
  if (squashing) {
    if (squashHoriz && squashVert) { rx = 7; ry = 7; }
    else if (squashHoriz)          { rx = 6; ry = 9; }   // hit a side wall
    else                           { rx = 9; ry = 6; }   // hit the top or bottom
    squashTimer--;
  }

  bool blink = (animTick % 60) < 3;
  drawSmiley((int)(bx + 0.5f), (int)(by + 0.5f), rx, ry, squashing || blink);

  u8g2.sendBuffer();
  animTick++;
}

void showWifiFailed() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(4, 30, "WiFi failed");
  u8g2.sendBuffer();
}

// ---------- OLED: your drawing ----------
void drawFrame() {
  u8g2.clearBuffer();
  if (frameInverted) {
    uint8_t tmp[1024];
    for (int i = 0; i < 1024; i++) tmp[i] = ~frame[i];
    u8g2.drawXBM(0, 0, 128, 64, tmp);
  } else {
    u8g2.drawXBM(0, 0, 128, 64, frame);
  }
  u8g2.sendBuffer();
}

// ---------- Web handlers ----------
int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleFrame() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data");
    return;
  }
  String body = server.arg("plain");
  if (body.length() != 2048) {
    server.send(400, "text/plain", "Bad size");
    return;
  }
  for (int i = 0; i < 1024; i++) {
    int hi = hexVal(body[2 * i]);
    int lo = hexVal(body[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      server.send(400, "text/plain", "Bad data");
      return;
    }
    frame[i] = (hi << 4) | lo;
  }
  frameInverted = (server.arg("inv") == "1");
  frameDirty = true;
  server.send(200, "text/plain", "ok");
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(400000);
  u8g2.begin();

  WiFi.mode(WIFI_AP);
  apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  ipText = WiFi.softAPIP().toString();

  Serial.println(apOk ? "Hotspot started" : "Hotspot FAILED (password needs 8+ characters)");
  Serial.print("WiFi name: "); Serial.println(AP_SSID);
  Serial.print("Open: http://"); Serial.println(ipText);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/frame", HTTP_POST, handleFrame);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();

  if (!apOk) showWifiFailed();
}

void loop() {
  server.handleClient();

  if (frameDirty) {
    frameDirty = false;
    gotFrame = true;                 // stop the startup animation for good
    drawFrame();
  } else if (!gotFrame && apOk && millis() - lastAnim >= ANIM_DELAY_MS) {
    lastAnim = millis();
    drawStartupFrame();
  }
}
