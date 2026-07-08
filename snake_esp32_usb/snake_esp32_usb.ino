/*
 * SNAKE - ESP32-S3 + OLED SSD1306 (I2C) - controle clavier via WiFi + WebSocket
 *
 * Fonctionnalites :
 *   - Traversee des murs : sortir d'un cote = revenir de l'autre.
 *   - 3 modes :
 *       FACILE     : terrain vide.
 *       DIFFICILE  : obstacles mobiles a eviter + serpent plus rapide.
 *       IMPOSSIBLE : un serpent rival te pourchasse. Il est rapide mais ne
 *                    traverse pas les murs : tu peux toujours t'echapper en
 *                    passant par un bord. Tres dur mais pas injouable.
 *   - Style pixel art : tete avec yeux, corps texture, pomme, bordure, scores.
 *
 * POUR JOUER :
 *   1. Connecte ton PC/telephone au WiFi :  SnakeESP  (mot de passe : snake1234)
 *   2. Ouvre un navigateur sur :  http://192.168.4.1
 *   3. Choisis un mode, puis joue avec Z Q S D. R = rejouer.
 *
 * LIBRAIRIES : WebSockets (Markus Sattler), Adafruit GFX, Adafruit SSD1306
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* AP_SSID = "SnakeESP";
const char* AP_PASS = "snake1234";

WebServer        server(80);
WebSocketsServer webSocket(81);

// Terrain : bandeau de score en haut + bordure d'1 px.
#define CELL    5
#define TOP     10
#define OX      1
#define OY      (TOP + 1)
#define GRID_W  ((SCREEN_WIDTH - 2) / CELL)
#define GRID_H  ((SCREEN_HEIGHT - TOP - 2) / CELL)
#define MAX_LEN (GRID_W * GRID_H)

#define NB_OBST 3            // obstacles en mode difficile

enum Dir  { UP, DOWN, LEFT, RIGHT };
enum Mode { EASY, HARD, IMPOSSIBLE };

int  snakeX[MAX_LEN];
int  snakeY[MAX_LEN];
int  snakeLen;
Dir  dir;
Dir  nextDir;
int  foodX, foodY;
int  score;
int  best = 0;
bool gameOver;
bool started = false;
Mode mode = EASY;
unsigned long lastMove;
unsigned long moveDelay;

// obstacles mobiles (mode DIFFICILE)
int  obstX[NB_OBST], obstY[NB_OBST];
int  obstDX[NB_OBST], obstDY[NB_OBST];
unsigned long lastObst;
unsigned long obstDelay = 400;

// serpent rival (mode IMPOSSIBLE) : 4 segments, chasse la tete du joueur
#define RIVAL_LEN 4
int  rivalX[RIVAL_LEN], rivalY[RIVAL_LEN];
unsigned long lastRival;
unsigned long rivalDelay = 150;   // un peu plus lent que le joueur -> laisse une chance

void draw();
void drawTitle();

bool cellFree(int x, int y) {
  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == x && snakeY[i] == y) return false;
  if (mode == HARD)
    for (int i = 0; i < NB_OBST; i++)
      if (obstX[i] == x && obstY[i] == y) return false;
  if (mode == IMPOSSIBLE)
    for (int i = 0; i < RIVAL_LEN; i++)
      if (rivalX[i] == x && rivalY[i] == y) return false;
  return true;
}

void placeFood() {
  do {
    foodX = random(GRID_W);
    foodY = random(GRID_H);
  } while (!cellFree(foodX, foodY));
}

void initObstacles() {
  for (int i = 0; i < NB_OBST; i++) {
    obstX[i] = random(2, GRID_W - 2);
    obstY[i] = random(2, GRID_H - 2);
    obstDX[i] = (random(2) ? 1 : -1);
    obstDY[i] = (random(2) ? 1 : -1);
  }
}

// place le rival loin de la tete du joueur, en ligne (comme un mini-serpent)
void initRival() {
  int rx = (GRID_W / 2 < 6) ? GRID_W - 2 : 2;
  int ry = 2;
  for (int i = 0; i < RIVAL_LEN; i++) { rivalX[i] = rx - i; rivalY[i] = ry; }
}

void resetGame() {
  snakeLen = 3;
  snakeX[0] = GRID_W / 2;     snakeY[0] = GRID_H / 2;
  snakeX[1] = GRID_W / 2 - 1; snakeY[1] = GRID_H / 2;
  snakeX[2] = GRID_W / 2 - 2; snakeY[2] = GRID_H / 2;
  dir = RIGHT;
  nextDir = RIGHT;
  score = 0;
  gameOver = false;

  if      (mode == EASY)       moveDelay = 190;
  else if (mode == HARD)       moveDelay = 130;
  else                          moveDelay = 110;   // IMPOSSIBLE : joueur rapide aussi

  lastMove  = millis();
  lastObst  = millis();
  lastRival = millis();

  if (mode == HARD)       initObstacles();
  if (mode == IMPOSSIBLE) initRival();

  placeFood();
  draw();
}

void setDir(Dir d) {
  if (d == UP    && dir == DOWN)  return;
  if (d == DOWN  && dir == UP)    return;
  if (d == LEFT  && dir == RIGHT) return;
  if (d == RIGHT && dir == LEFT)  return;
  nextDir = d;
}

void chooseMode(Mode m) {
  mode = m;
  started = true;
  resetGame();
}

// commande WebSocket : U D L R (dir), X (restart), E/H/I (modes)
void handleCommand(char c) {
  if (!started) {
    if (c == 'E') chooseMode(EASY);
    if (c == 'H') chooseMode(HARD);
    if (c == 'I') chooseMode(IMPOSSIBLE);
    return;
  }
  switch (c) {
    case 'U': setDir(UP);    break;
    case 'D': setDir(DOWN);  break;
    case 'L': setDir(LEFT);  break;
    case 'R': setDir(RIGHT); break;
    case 'E': chooseMode(EASY);       break;
    case 'H': chooseMode(HARD);       break;
    case 'I': chooseMode(IMPOSSIBLE); break;
    case 'X': if (gameOver) resetGame(); break;
  }
}

void moveObstacles() {
  for (int i = 0; i < NB_OBST; i++) {
    int nx = obstX[i] + obstDX[i];
    int ny = obstY[i] + obstDY[i];
    if (nx < 0 || nx >= GRID_W) { obstDX[i] = -obstDX[i]; nx = obstX[i] + obstDX[i]; }
    if (ny < 0 || ny >= GRID_H) { obstDY[i] = -obstDY[i]; ny = obstY[i] + obstDY[i]; }
    obstX[i] = nx;
    obstY[i] = ny;
    if (obstX[i] == snakeX[0] && obstY[i] == snakeY[0]) gameOver = true;
  }
}

// le rival avance d'une case vers la tete du joueur (chasse simple, sans
// traverser les murs) : il choisit l'axe ou l'ecart est le plus grand.
void moveRival() {
  int hx = rivalX[0], hy = rivalY[0];
  int dx = snakeX[0] - hx;
  int dy = snakeY[0] - hy;
  int nx = hx, ny = hy;

  if (abs(dx) >= abs(dy)) nx += (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
  else                    ny += (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

  // le rival NE traverse PAS les murs -> ca laisse une echappatoire au joueur
  if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) return;

  for (int i = RIVAL_LEN - 1; i > 0; i--) { rivalX[i] = rivalX[i-1]; rivalY[i] = rivalY[i-1]; }
  rivalX[0] = nx;
  rivalY[0] = ny;

  // contact avec n'importe quel segment du joueur -> perdu
  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == nx && snakeY[i] == ny) { gameOver = true; return; }
}

void moveSnake() {
  dir = nextDir;
  int newX = snakeX[0];
  int newY = snakeY[0];
  switch (dir) {
    case UP:    newY--; break;
    case DOWN:  newY++; break;
    case LEFT:  newX--; break;
    case RIGHT: newX++; break;
  }

  // TRAVERSEE DES MURS : le joueur, lui, repasse de l'autre cote
  if (newX < 0)        newX = GRID_W - 1;
  if (newX >= GRID_W)  newX = 0;
  if (newY < 0)        newY = GRID_H - 1;
  if (newY >= GRID_H)  newY = 0;

  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == newX && snakeY[i] == newY) { gameOver = true; return; }

  if (mode == HARD)
    for (int i = 0; i < NB_OBST; i++)
      if (obstX[i] == newX && obstY[i] == newY) { gameOver = true; return; }

  if (mode == IMPOSSIBLE)
    for (int i = 0; i < RIVAL_LEN; i++)
      if (rivalX[i] == newX && rivalY[i] == newY) { gameOver = true; return; }

  for (int i = snakeLen; i > 0; i--) { snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1]; }
  snakeX[0] = newX;
  snakeY[0] = newY;

  if (newX == foodX && newY == foodY) {
    if (snakeLen < MAX_LEN) snakeLen++;
    score++;
    if (score > best) best = score;
    if (moveDelay > 70) moveDelay -= 6;
    placeFood();
  }
}

// --- rendu ---
inline int px(int cx) { return OX + cx * CELL; }
inline int py(int cy) { return OY + cy * CELL; }

void drawHead(int cx, int cy) {
  int x = px(cx), y = py(cy);
  display.fillRoundRect(x, y, CELL, CELL, 1, SSD1306_WHITE);
  int ex1, ey1, ex2, ey2;
  switch (dir) {
    case LEFT:  ex1=x+1; ey1=y+1; ex2=x+1; ey2=y+CELL-2; break;
    case RIGHT: ex1=x+CELL-2; ey1=y+1; ex2=x+CELL-2; ey2=y+CELL-2; break;
    case UP:    ex1=x+1; ey1=y+1; ex2=x+CELL-2; ey2=y+1; break;
    default:    ex1=x+1; ey1=y+CELL-2; ex2=x+CELL-2; ey2=y+CELL-2; break;
  }
  display.drawPixel(ex1, ey1, SSD1306_BLACK);
  display.drawPixel(ex2, ey2, SSD1306_BLACK);
}

void drawBody(int cx, int cy) {
  int x = px(cx), y = py(cy);
  display.fillRect(x, y, CELL, CELL, SSD1306_WHITE);
  display.drawPixel(x + CELL/2, y + CELL/2, SSD1306_BLACK);
}

void drawFood(int cx, int cy) {
  int x = px(cx), y = py(cy);
  int r = CELL/2;
  display.fillCircle(x + r, y + r, r, SSD1306_WHITE);
  display.drawPixel(x + r, y, SSD1306_WHITE);
}

void drawObstacle(int cx, int cy) {
  int x = px(cx), y = py(cy);
  display.drawRect(x, y, CELL, CELL, SSD1306_WHITE);
  display.drawLine(x, y, x + CELL - 1, y + CELL - 1, SSD1306_WHITE);
  display.drawLine(x, y + CELL - 1, x + CELL - 1, y, SSD1306_WHITE);
}

// rival : corps en damier (pointille) pour bien le distinguer du joueur
void drawRival(int cx, int cy, bool headSeg) {
  int x = px(cx), y = py(cy);
  if (headSeg) {
    display.drawRect(x, y, CELL, CELL, SSD1306_WHITE);
    display.drawRect(x+1, y+1, CELL-2, CELL-2, SSD1306_WHITE);
  } else {
    for (int i = 0; i < CELL; i++)
      for (int j = 0; j < CELL; j++)
        if ((i + j) % 2 == 0) display.drawPixel(x + i, y + j, SSD1306_WHITE);
  }
}

void draw() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 1);
  display.print("SC ");
  display.print(score);
  display.setCursor(44, 1);
  if      (mode == EASY)       display.print("EASY");
  else if (mode == HARD)       display.print("DIFF");
  else                          display.print("IMPO");
  display.setCursor(SCREEN_WIDTH - 38, 1);
  display.print("HI ");
  display.print(best);
  display.drawLine(0, TOP - 1, SCREEN_WIDTH - 1, TOP - 1, SSD1306_WHITE);

  display.drawRect(0, TOP, SCREEN_WIDTH, SCREEN_HEIGHT - TOP, SSD1306_WHITE);

  if (gameOver) {
    display.fillRect(24, 26, 80, 26, SSD1306_BLACK);
    display.drawRect(24, 26, 80, 26, SSD1306_WHITE);
    display.setCursor(40, 30);
    display.print("GAME OVER");
    display.setCursor(30, 42);
    display.print("Score: ");
    display.print(score);
    display.display();
    return;
  }

  drawFood(foodX, foodY);
  if (mode == HARD)
    for (int i = 0; i < NB_OBST; i++) drawObstacle(obstX[i], obstY[i]);
  if (mode == IMPOSSIBLE)
    for (int i = 0; i < RIVAL_LEN; i++) drawRival(rivalX[i], rivalY[i], i == 0);

  drawHead(snakeX[0], snakeY[0]);
  for (int i = 1; i < snakeLen; i++) drawBody(snakeX[i], snakeY[i]);

  display.display();
}

void drawTitle() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(24, 6);
  display.print("SNAKE");
  display.setTextSize(1);
  display.setCursor(6, 30);
  display.print("Choisis un mode :");
  display.setCursor(6, 42);
  display.print("Facile / Difficile");
  display.setCursor(6, 52);
  display.print("/ Impossible");
  display.display();
}

// ---- Page web ----
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Snake ESP32</title>
<style>
 body{font-family:sans-serif;text-align:center;background:#0d1117;color:#eee;margin:0;padding:20px}
 h2{margin:10px;color:#5ee27a}
 #etat{font-size:14px;color:#8f8;margin:6px}
 .modes{margin:12px}
 .modes button{padding:10px 16px;font-size:15px;border:0;border-radius:10px;margin:4px;background:#238636;color:#fff}
 .modes button.h{background:#a86a20}
 .modes button.i{background:#a83232}
 .pad{display:inline-grid;grid-template-columns:repeat(3,70px);gap:8px;margin-top:16px}
 .pad button{height:70px;font-size:26px;border:0;border-radius:12px;background:#21262d;color:#eee}
 .pad button:active{background:#30363d}
 .hint{color:#888;font-size:14px;margin-top:14px}
</style></head><body>
<h2>Snake ESP32</h2>
<div id="etat">Connexion...</div>
<div class="modes">
 <button onclick="s('E')">Facile</button>
 <button class="h" onclick="s('H')">Difficile</button>
 <button class="i" onclick="s('I')">Impossible</button>
</div>
<p>Clavier : <b>Z Q S D</b> (ou les fleches). <b>R</b> = rejouer.</p>
<div class="pad">
 <span></span><button onclick="s('U')">&uarr;</button><span></span>
 <button onclick="s('L')">&larr;</button><button onclick="s('X')">R</button><button onclick="s('R')">&rarr;</button>
 <span></span><button onclick="s('D')">&darr;</button><span></span>
</div>
<p class="hint">Le jeu s'affiche sur l'ecran OLED.</p>
<script>
 var ws = new WebSocket('ws://' + location.hostname + ':81/');
 ws.onopen  = function(){ document.getElementById('etat').textContent = 'Connecte'; };
 ws.onclose = function(){ document.getElementById('etat').textContent = 'Deconnecte'; };
 function s(d){ if(ws.readyState===1) ws.send(d); }
 document.addEventListener('keydown',function(e){
   var k=e.key.toLowerCase();
   if(k==='z'||k==='w'||e.key==='ArrowUp'){s('U');e.preventDefault();}
   else if(k==='s'||e.key==='ArrowDown'){s('D');e.preventDefault();}
   else if(k==='q'||k==='a'||e.key==='ArrowLeft'){s('L');e.preventDefault();}
   else if(k==='d'||e.key==='ArrowRight'){s('R');e.preventDefault();}
   else if(k==='r'){s('X');}
   else if(k==='1'){s('E');}
   else if(k==='2'){s('H');}
   else if(k==='3'){s('I');}
 });
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT && length > 0) handleCommand((char)payload[0]);
}

bool initDisplayAuto() {
  int sda[2] = {17, 18};
  int scl[2] = {18, 17};
  uint8_t addr[2] = {0x3C, 0x3D};
  for (int p = 0; p < 2; p++) {
    Wire.end();
    Wire.begin(sda[p], scl[p]);
    delay(50);
    for (int a = 0; a < 2; a++)
      if (display.begin(SSD1306_SWITCHCAPVCC, addr[a], true, false)) return true;
  }
  return false;
}

void showIntro(IPAddress ip) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);  display.print("WiFi: ");  display.print(AP_SSID);
  display.setCursor(0, 12); display.print("Pass: ");  display.print(AP_PASS);
  display.setCursor(0, 28); display.print("Ouvre dans le navi:");
  display.setCursor(0, 40); display.print("http://"); display.print(ip);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!initDisplayAuto()) {
    Serial.println("OLED introuvable - verifie l'alimentation et les fils.");
    while (true) delay(10);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Reseau WiFi: "); Serial.println(AP_SSID);
  Serial.print("Ouvre http://"); Serial.println(ip);

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWsEvent);

  showIntro(ip);
  delay(4000);

  randomSeed(esp_random());
  drawTitle();
}

void loop() {
  webSocket.loop();
  server.handleClient();

  if (started && !gameOver) {
    unsigned long now = millis();

    if (mode == HARD && now - lastObst >= obstDelay) {
      lastObst = now;
      moveObstacles();
    }
    if (mode == IMPOSSIBLE && now - lastRival >= rivalDelay) {
      lastRival = now;
      moveRival();
    }
    if (now - lastMove >= moveDelay) {
      lastMove = now;
      moveSnake();
      draw();
    }
  }
}
