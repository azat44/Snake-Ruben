/*
 * SNAKE - ESP32-S3 + OLED SSD1306 (I2C) - controle clavier via WiFi + WebSocket
 *
 * Fonctionnalites :
 *   - Traversee des murs : sortir d'un cote = revenir de l'autre.
 *   - 4 modes :
 *       FACILE     : terrain vide.
 *       DIFFICILE  : obstacles mobiles a eviter + serpent plus rapide.
 *       IMPOSSIBLE : un serpent rival te pourchasse. Il est rapide mais ne
 *                    traverse pas les murs : tu peux toujours t'echapper en
 *                    passant par un bord. Tres dur mais pas injouable.
 *       MULTI      : 2 joueurs sur le meme ecran + rival IA.
 *                    J1 = fleches, J2 = ZQSD. Le rival chasse le plus proche.
 *   - Style pixel art : tete avec yeux, corps texture, pomme, bordure, scores.
 *
 * POUR JOUER :
 *   1. Connecte ton PC/telephone au WiFi :  SnakeESP  (mot de passe : snake1234)
 *   2. Ouvre un navigateur sur :  http://192.168.4.1
 *   3. Choisis un mode, puis joue. R = rejouer.
 *      Solo   : Z Q S D ou fleches.
 *      Multi  : J1 = fleches, J2 = Z Q S D.
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
#define MAX_RIVAL 20         // longueur max du rival (grandit en multi)

enum Dir  { UP, DOWN, LEFT, RIGHT };
enum Mode { EASY, HARD, IMPOSSIBLE, MULTI };

// --- Joueur 1 (serpent principal, tous modes) ---
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

// --- Joueur 2 (mode MULTI uniquement) ---
int  p2X[MAX_LEN];
int  p2Y[MAX_LEN];
int  p2Len;
Dir  p2Dir;
Dir  p2NextDir;
int  p2Score;
bool p2Alive;
bool p1Alive;

// obstacles mobiles (mode DIFFICILE)
int  obstX[NB_OBST], obstY[NB_OBST];
int  obstDX[NB_OBST], obstDY[NB_OBST];
unsigned long lastObst;
unsigned long obstDelay = 400;

// serpent rival (modes IMPOSSIBLE + MULTI)
int  rivalX[MAX_RIVAL], rivalY[MAX_RIVAL];
int  rivalLen;
unsigned long lastRival;
unsigned long rivalDelay = 150;

// multi
String winner = "";
unsigned long gameOverTime = 0;

void draw();
void drawMulti();
void drawTitle();

bool cellFree(int x, int y) {
  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == x && snakeY[i] == y) return false;
  if (mode == MULTI)
    for (int i = 0; i < p2Len; i++)
      if (p2X[i] == x && p2Y[i] == y) return false;
  if (mode == HARD)
    for (int i = 0; i < NB_OBST; i++)
      if (obstX[i] == x && obstY[i] == y) return false;
  if (mode == IMPOSSIBLE || mode == MULTI)
    for (int i = 0; i < rivalLen; i++)
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

void initRival() {
  rivalLen = 4;
  int rx = GRID_W - 3;
  int ry = 2;
  for (int i = 0; i < rivalLen; i++) { rivalX[i] = rx - i; rivalY[i] = ry; }
}

void initP2() {
  p2Len = 3;
  p2X[0] = GRID_W - 4;     p2Y[0] = GRID_H - 3;
  p2X[1] = GRID_W - 3;     p2Y[1] = GRID_H - 3;
  p2X[2] = GRID_W - 2;     p2Y[2] = GRID_H - 3;
  p2Dir = LEFT;
  p2NextDir = LEFT;
  p2Score = 0;
  p2Alive = true;
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
  p1Alive = true;
  winner = "";

  if      (mode == EASY)       moveDelay = 190;
  else if (mode == HARD)       moveDelay = 130;
  else if (mode == IMPOSSIBLE) moveDelay = 110;
  else                         moveDelay = 140;   // MULTI

  lastMove  = millis();
  lastObst  = millis();
  lastRival = millis();

  if (mode == HARD)                      initObstacles();
  if (mode == IMPOSSIBLE || mode == MULTI) initRival();
  if (mode == MULTI)                     initP2();

  placeFood();

  if (mode == MULTI) drawMulti();
  else               draw();
}

void setDir(Dir d) {
  if (d == UP    && dir == DOWN)  return;
  if (d == DOWN  && dir == UP)    return;
  if (d == LEFT  && dir == RIGHT) return;
  if (d == RIGHT && dir == LEFT)  return;
  nextDir = d;
}

void setP2Dir(Dir d) {
  if (d == UP    && p2Dir == DOWN)  return;
  if (d == DOWN  && p2Dir == UP)    return;
  if (d == LEFT  && p2Dir == RIGHT) return;
  if (d == RIGHT && p2Dir == LEFT)  return;
  p2NextDir = d;
}

void chooseMode(Mode m) {
  mode = m;
  started = true;
  resetGame();
}

// commande WebSocket :
//   Majuscules U D L R = fleches (P1 en multi, tout le monde en solo)
//   Minuscules u d l r = ZQSD   (P2 en multi, tout le monde en solo)
//   X = restart, E/H/I/M = modes
void handleCommand(char c) {
  if (!started) {
    if (c == 'E') chooseMode(EASY);
    if (c == 'H') chooseMode(HARD);
    if (c == 'I') chooseMode(IMPOSSIBLE);
    if (c == 'M') chooseMode(MULTI);
    return;
  }

  // Restart
  if (c == 'X') { if (gameOver) resetGame(); return; }

  // Mode selection (meme en cours de partie)
  if (c == 'E') { chooseMode(EASY);       return; }
  if (c == 'H') { chooseMode(HARD);       return; }
  if (c == 'I') { chooseMode(IMPOSSIBLE); return; }
  if (c == 'M') { chooseMode(MULTI);      return; }

  if (mode == MULTI) {
    // En multi : majuscules = J1 (fleches), minuscules = J2 (ZQSD)
    if (p1Alive) {
      if (c == 'U') setDir(UP);
      if (c == 'D') setDir(DOWN);
      if (c == 'L') setDir(LEFT);
      if (c == 'R') setDir(RIGHT);
    }
    if (p2Alive) {
      if (c == 'u') setP2Dir(UP);
      if (c == 'd') setP2Dir(DOWN);
      if (c == 'l') setP2Dir(LEFT);
      if (c == 'r') setP2Dir(RIGHT);
    }
  } else {
    // En solo : les deux sets de touches controlent le meme serpent
    switch (c) {
      case 'U': case 'u': setDir(UP);    break;
      case 'D': case 'd': setDir(DOWN);  break;
      case 'L': case 'l': setDir(LEFT);  break;
      case 'R': case 'r': setDir(RIGHT); break;
    }
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

// le rival chasse le joueur le plus proche (en multi) ou le joueur unique (en impossible)
void moveRival() {
  int hx = rivalX[0], hy = rivalY[0];
  int tx, ty;

  if (mode == MULTI) {
    // cibler le joueur vivant le plus proche
    int d1 = p1Alive ? (abs(hx - snakeX[0]) + abs(hy - snakeY[0])) : 9999;
    int d2 = p2Alive ? (abs(hx - p2X[0])    + abs(hy - p2Y[0]))    : 9999;
    if (d1 <= d2 && p1Alive) { tx = snakeX[0]; ty = snakeY[0]; }
    else if (p2Alive)        { tx = p2X[0];    ty = p2Y[0]; }
    else return;
  } else {
    tx = snakeX[0]; ty = snakeY[0];
  }

  int dx = tx - hx;
  int dy = ty - hy;
  int nx = hx, ny = hy;

  if (abs(dx) >= abs(dy)) nx += (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
  else                    ny += (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

  if (mode == MULTI) {
    // en multi le rival traverse les murs (plus menaçant)
    if (nx < 0)        nx = GRID_W - 1;
    if (nx >= GRID_W)  nx = 0;
    if (ny < 0)        ny = GRID_H - 1;
    if (ny >= GRID_H)  ny = 0;
  } else {
    // en impossible le rival NE traverse PAS les murs -> echappatoire
    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) return;
  }

  for (int i = rivalLen - 1; i > 0; i--) { rivalX[i] = rivalX[i-1]; rivalY[i] = rivalY[i-1]; }
  rivalX[0] = nx;
  rivalY[0] = ny;

  // contact avec J1
  if (p1Alive) {
    for (int i = 0; i < snakeLen; i++)
      if (snakeX[i] == nx && snakeY[i] == ny) {
        if (mode == MULTI) { p1Alive = false; }
        else               { gameOver = true; }
        return;
      }
  }
  // contact avec J2 (multi)
  if (mode == MULTI && p2Alive) {
    for (int i = 0; i < p2Len; i++)
      if (p2X[i] == nx && p2Y[i] == ny) { p2Alive = false; return; }
  }
}

void moveSnake() {
  if (mode == MULTI && !p1Alive) return;

  dir = nextDir;
  int newX = snakeX[0];
  int newY = snakeY[0];
  switch (dir) {
    case UP:    newY--; break;
    case DOWN:  newY++; break;
    case LEFT:  newX--; break;
    case RIGHT: newX++; break;
  }

  // TRAVERSEE DES MURS
  if (newX < 0)        newX = GRID_W - 1;
  if (newX >= GRID_W)  newX = 0;
  if (newY < 0)        newY = GRID_H - 1;
  if (newY >= GRID_H)  newY = 0;

  // collision avec soi-meme
  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == newX && snakeY[i] == newY) {
      if (mode == MULTI) { p1Alive = false; return; }
      else               { gameOver = true; return; }
    }

  // collision avec obstacles (HARD)
  if (mode == HARD)
    for (int i = 0; i < NB_OBST; i++)
      if (obstX[i] == newX && obstY[i] == newY) { gameOver = true; return; }

  // collision avec rival (IMPOSSIBLE / MULTI)
  if (mode == IMPOSSIBLE || mode == MULTI)
    for (int i = 0; i < rivalLen; i++)
      if (rivalX[i] == newX && rivalY[i] == newY) {
        if (mode == MULTI) { p1Alive = false; return; }
        else               { gameOver = true; return; }
      }

  // collision avec J2 (MULTI)
  if (mode == MULTI && p2Alive)
    for (int i = 0; i < p2Len; i++)
      if (p2X[i] == newX && p2Y[i] == newY) { p1Alive = false; return; }

  // avancer
  for (int i = snakeLen; i > 0; i--) { snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1]; }
  snakeX[0] = newX;
  snakeY[0] = newY;

  // manger
  if (newX == foodX && newY == foodY) {
    if (snakeLen < MAX_LEN) snakeLen++;
    score++;
    if (score > best) best = score;
    if (mode != MULTI && moveDelay > 70) moveDelay -= 6;
    // le rival grandit en multi
    if (mode == MULTI && rivalLen < MAX_RIVAL) rivalLen++;
    placeFood();
  }
}

void moveP2() {
  if (!p2Alive) return;

  p2Dir = p2NextDir;
  int newX = p2X[0];
  int newY = p2Y[0];
  switch (p2Dir) {
    case UP:    newY--; break;
    case DOWN:  newY++; break;
    case LEFT:  newX--; break;
    case RIGHT: newX++; break;
  }

  // traversee des murs
  if (newX < 0)        newX = GRID_W - 1;
  if (newX >= GRID_W)  newX = 0;
  if (newY < 0)        newY = GRID_H - 1;
  if (newY >= GRID_H)  newY = 0;

  // collision avec soi
  for (int i = 0; i < p2Len; i++)
    if (p2X[i] == newX && p2Y[i] == newY) { p2Alive = false; return; }

  // collision avec rival
  for (int i = 0; i < rivalLen; i++)
    if (rivalX[i] == newX && rivalY[i] == newY) { p2Alive = false; return; }

  // collision avec J1
  if (p1Alive)
    for (int i = 0; i < snakeLen; i++)
      if (snakeX[i] == newX && snakeY[i] == newY) { p2Alive = false; return; }

  // avancer
  for (int i = p2Len; i > 0; i--) { p2X[i] = p2X[i-1]; p2Y[i] = p2Y[i-1]; }
  p2X[0] = newX;
  p2Y[0] = newY;

  // manger
  if (newX == foodX && newY == foodY) {
    if (p2Len < MAX_LEN) p2Len++;
    p2Score++;
    if (rivalLen < MAX_RIVAL) rivalLen++;
    placeFood();
  }
}

// Verifier fin de partie en multi
void checkMultiGameOver() {
  if (!p1Alive && !p2Alive) {
    gameOver = true;
    winner = "EGALITE!";
    gameOverTime = millis();
  } else if (!p1Alive) {
    gameOver = true;
    winner = "J2 GAGNE!";
    gameOverTime = millis();
  } else if (!p2Alive) {
    gameOver = true;
    winner = "J1 GAGNE!";
    gameOverTime = millis();
  }
}

// --- rendu ---
inline int px(int cx) { return OX + cx * CELL; }
inline int py(int cy) { return OY + cy * CELL; }

void drawHead(int cx, int cy, Dir d) {
  int x = px(cx), y = py(cy);
  display.fillRoundRect(x, y, CELL, CELL, 1, SSD1306_WHITE);
  int ex1, ey1, ex2, ey2;
  switch (d) {
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

// corps J2 : contour uniquement (distinguer de J1 qui est plein)
void drawP2Head(int cx, int cy, Dir d) {
  int x = px(cx), y = py(cy);
  display.drawRoundRect(x, y, CELL, CELL, 1, SSD1306_WHITE);
  // yeux selon la direction
  int ex1, ey1, ex2, ey2;
  switch (d) {
    case LEFT:  ex1=x+1; ey1=y+1; ex2=x+1; ey2=y+CELL-2; break;
    case RIGHT: ex1=x+CELL-2; ey1=y+1; ex2=x+CELL-2; ey2=y+CELL-2; break;
    case UP:    ex1=x+1; ey1=y+1; ex2=x+CELL-2; ey2=y+1; break;
    default:    ex1=x+1; ey1=y+CELL-2; ex2=x+CELL-2; ey2=y+CELL-2; break;
  }
  display.drawPixel(ex1, ey1, SSD1306_WHITE);
  display.drawPixel(ex2, ey2, SSD1306_WHITE);
}

void drawP2Body(int cx, int cy) {
  int x = px(cx), y = py(cy);
  // contour + croix au centre (style different de J1)
  display.drawRect(x, y, CELL, CELL, SSD1306_WHITE);
  display.drawPixel(x + CELL/2, y + CELL/2, SSD1306_WHITE);
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

// rival : damier
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

// Rendu solo (EASY / HARD / IMPOSSIBLE)
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
    for (int i = 0; i < rivalLen; i++) drawRival(rivalX[i], rivalY[i], i == 0);

  drawHead(snakeX[0], snakeY[0], dir);
  for (int i = 1; i < snakeLen; i++) drawBody(snakeX[i], snakeY[i]);

  display.display();
}

// Rendu multi
void drawMulti() {
  display.clearDisplay();

  // Barre de score : J1:XX  MULTI  J2:XX
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 1);
  display.print("J1:");
  display.print(score);
  if (!p1Alive) display.print("X");
  display.setCursor(46, 1);
  display.print("MULTI");
  display.setCursor(SCREEN_WIDTH - 30, 1);
  display.print("J2:");
  display.print(p2Score);
  if (!p2Alive) display.print("X");
  display.drawLine(0, TOP - 1, SCREEN_WIDTH - 1, TOP - 1, SSD1306_WHITE);
  display.drawRect(0, TOP, SCREEN_WIDTH, SCREEN_HEIGHT - TOP, SSD1306_WHITE);

  if (gameOver) {
    display.fillRect(14, 22, 100, 30, SSD1306_BLACK);
    display.drawRect(14, 22, 100, 30, SSD1306_WHITE);
    display.setCursor(20, 26);
    display.print(winner);
    display.setCursor(20, 40);
    display.print(score);
    display.print(" - ");
    display.print(p2Score);
    display.display();
    return;
  }

  // Nourriture
  drawFood(foodX, foodY);

  // Rival
  for (int i = 0; i < rivalLen; i++) drawRival(rivalX[i], rivalY[i], i == 0);

  // J1 (plein)
  if (p1Alive) {
    drawHead(snakeX[0], snakeY[0], dir);
    for (int i = 1; i < snakeLen; i++) drawBody(snakeX[i], snakeY[i]);
  }

  // J2 (contour)
  if (p2Alive) {
    drawP2Head(p2X[0], p2Y[0], p2Dir);
    for (int i = 1; i < p2Len; i++) drawP2Body(p2X[i], p2Y[i]);
  }

  display.display();
}

void drawTitle() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(24, 4);
  display.print("SNAKE");
  display.setTextSize(1);
  display.setCursor(6, 26);
  display.print("Choisis un mode :");
  display.setCursor(6, 38);
  display.print("Facile / Difficile");
  display.setCursor(6, 48);
  display.print("/ Impossible / Multi");
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
 .modes{margin:12px;display:flex;flex-wrap:wrap;justify-content:center;gap:6px}
 .modes button{padding:10px 14px;font-size:14px;border:0;border-radius:10px;margin:2px;background:#238636;color:#fff;cursor:pointer}
 .modes button.h{background:#a86a20}
 .modes button.i{background:#a83232}
 .modes button.m{background:#6f42c1}
 .controls{margin-top:14px}
 .controls-label{font-size:13px;color:#aaa;margin-bottom:6px}
 .pads{display:flex;gap:20px;justify-content:center;flex-wrap:wrap;margin-top:10px}
 .pad-zone{display:flex;flex-direction:column;align-items:center;gap:4px}
 .pad-title{font-size:13px;font-weight:700}
 .p1t{color:#5ee27a}
 .p2t{color:#f0883e}
 .pad{display:inline-grid;grid-template-columns:repeat(3,60px);gap:6px}
 .pad button{height:60px;font-size:22px;border:0;border-radius:12px;background:#21262d;color:#eee;cursor:pointer}
 .pad button:active{background:#30363d}
 .restart-row{margin-top:14px}
 .restart-row button{padding:10px 28px;font-size:15px;border:2px solid #5ee27a;background:transparent;color:#5ee27a;border-radius:8px;cursor:pointer}
 .hint{color:#666;font-size:12px;margin-top:12px;line-height:1.5}
</style></head><body>
<h2>SNAKE ESP32</h2>
<div id="etat">Connexion...</div>
<div class="modes">
 <button onclick="s('E')">Facile</button>
 <button class="h" onclick="s('H')">Difficile</button>
 <button class="i" onclick="s('I')">Impossible</button>
 <button class="m" onclick="s('M')">Multi 2J</button>
</div>
<div class="controls">
 <div class="controls-label">Solo : ZQSD ou fleches &nbsp;|&nbsp; Multi : J2 = ZQSD, J1 = fleches</div>
 <div class="pads">
  <div class="pad-zone">
   <div class="pad-title p2t">J2 — ZQSD</div>
   <div class="pad">
    <span></span><button ontouchstart="s('u')" onclick="s('u')">&uarr;</button><span></span>
    <button ontouchstart="s('l')" onclick="s('l')">&larr;</button><button onclick="s('X')">R</button><button ontouchstart="s('r')" onclick="s('r')">&rarr;</button>
    <span></span><button ontouchstart="s('d')" onclick="s('d')">&darr;</button><span></span>
   </div>
  </div>
  <div class="pad-zone">
   <div class="pad-title p1t">J1 — Fleches</div>
   <div class="pad">
    <span></span><button ontouchstart="s('U')" onclick="s('U')">&uarr;</button><span></span>
    <button ontouchstart="s('L')" onclick="s('L')">&larr;</button><button onclick="s('X')">R</button><button ontouchstart="s('R')" onclick="s('R')">&rarr;</button>
    <span></span><button ontouchstart="s('D')" onclick="s('D')">&darr;</button><span></span>
   </div>
  </div>
 </div>
</div>
<div class="restart-row"><button onclick="s('X')">REJOUER</button></div>
<p class="hint">Le jeu s'affiche sur l'ecran OLED de l'ESP32.<br>
Clavier : Fleches = J1, ZQSD = J2, R = Rejouer<br>
Touches 1/2/3/4 = changer de mode</p>
<script>
 var ws;
 function c(){
   ws = new WebSocket('ws://' + location.hostname + ':81/');
   ws.onopen  = function(){ document.getElementById('etat').textContent = 'Connecte !'; };
   ws.onclose = function(){
     document.getElementById('etat').textContent = 'Deconnecte...';
     setTimeout(c, 1500);
   };
 }
 c();
 function s(d){ if(ws && ws.readyState===1) ws.send(d); }
 document.addEventListener('keydown', function(e){
   var k = e.key;
   // Fleches → majuscules (J1)
   if(k==='ArrowUp'){s('U');e.preventDefault();}
   else if(k==='ArrowDown'){s('D');e.preventDefault();}
   else if(k==='ArrowLeft'){s('L');e.preventDefault();}
   else if(k==='ArrowRight'){s('R');e.preventDefault();}
   // ZQSD → minuscules (J2)
   else if(k==='z'||k==='Z'||k==='w'||k==='W'){s('u');e.preventDefault();}
   else if(k==='s'||k==='S'){s('d');e.preventDefault();}
   else if(k==='q'||k==='Q'||k==='a'||k==='A'){s('l');e.preventDefault();}
   else if(k==='d'||k==='D'){s('r');e.preventDefault();}
   // Autres
   else if(k==='r'||k==='R'){s('X');}
   else if(k==='1'){s('E');}
   else if(k==='2'){s('H');}
   else if(k==='3'){s('I');}
   else if(k==='4'){s('M');}
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
    if ((mode == IMPOSSIBLE || mode == MULTI) && now - lastRival >= rivalDelay) {
      lastRival = now;
      moveRival();
    }
    if (now - lastMove >= moveDelay) {
      lastMove = now;
      moveSnake();
      if (mode == MULTI) {
        moveP2();
        checkMultiGameOver();
        drawMulti();
      } else {
        draw();
      }
    }
  }

  // Restart auto en multi apres 5s
  if (mode == MULTI && gameOver && millis() - gameOverTime > 5000) {
    resetGame();
  }
}
