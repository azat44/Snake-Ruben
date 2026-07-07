/*
 * SNAKE - ESP32-S3 + OLED SSD1306 (I2C) - controle au clavier ZQSD par cable USB
 *
 * Pas de WiFi, pas d'adresse IP, pas de Python. On tape les touches dans un
 * terminal serie et le serpent bouge sur l'OLED.
 *
 * REGLAGE INDISPENSABLE (sinon les touches ne passent pas sur cette carte) :
 *   Tools > USB CDC On Boot  ->  Enabled       (a faire AVANT de televerser)
 *
 * POUR JOUER (fluide) : ouvre PuTTY en mode "Serial", COM11, 115200 bauds,
 *   puis appuie sur Z Q S D directement (pas besoin d'Entree). R = rejouer.
 *   Les fleches du clavier marchent aussi.
 *
 * Alternative sans rien installer : le Moniteur serie de l'IDE (115200),
 *   mais il faut taper la lettre puis Entree a chaque fois (moins fluide).
 *
 * Librairies : Adafruit GFX Library, Adafruit SSD1306
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Grille de jeu
#define CELL   4
#define HEADER 8
#define GRID_W (SCREEN_WIDTH / CELL)               // 32 colonnes
#define GRID_H ((SCREEN_HEIGHT - HEADER) / CELL)   // 14 lignes
#define MAX_LEN (GRID_W * GRID_H)

enum Dir { UP, DOWN, LEFT, RIGHT };

int  snakeX[MAX_LEN];
int  snakeY[MAX_LEN];
int  snakeLen;
Dir  dir;
Dir  nextDir;
int  foodX, foodY;
int  score;
bool gameOver;
unsigned long lastMove;
unsigned long moveDelay;

// parseur des fleches : ESC [ A/B/C/D
enum EscState { ES_NONE, ES_ESC, ES_BRACKET };
EscState escState = ES_NONE;

void draw();

void placeFood() {
  bool ok;
  do {
    ok = true;
    foodX = random(GRID_W);
    foodY = random(GRID_H);
    for (int i = 0; i < snakeLen; i++)
      if (snakeX[i] == foodX && snakeY[i] == foodY) { ok = false; break; }
  } while (!ok);
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
  moveDelay = 180;
  lastMove = millis();
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

void handleByte(char c) {
  // fleches : ESC [ A/B/C/D
  if (escState == ES_ESC) { escState = (c == '[') ? ES_BRACKET : ES_NONE; return; }
  if (escState == ES_BRACKET) {
    escState = ES_NONE;
    switch (c) {
      case 'A': setDir(UP);    return;
      case 'B': setDir(DOWN);  return;
      case 'C': setDir(RIGHT); return;
      case 'D': setDir(LEFT);  return;
    }
    return;
  }
  if (c == 0x1B) { escState = ES_ESC; return; }

  // ZQSD (et WASD)
  switch (c) {
    case 'z': case 'Z': case 'w': case 'W': setDir(UP);    break;
    case 's': case 'S':                     setDir(DOWN);  break;
    case 'q': case 'Q': case 'a': case 'A': setDir(LEFT);  break;
    case 'd': case 'D':                     setDir(RIGHT); break;
    case 'r': case 'R': if (gameOver) resetGame(); break;
  }
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
  if (newX < 0 || newX >= GRID_W || newY < 0 || newY >= GRID_H) { gameOver = true; return; }
  for (int i = 0; i < snakeLen; i++)
    if (snakeX[i] == newX && snakeY[i] == newY) { gameOver = true; return; }

  for (int i = snakeLen; i > 0; i--) { snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1]; }
  snakeX[0] = newX;
  snakeY[0] = newY;

  if (newX == foodX && newY == foodY) {
    if (snakeLen < MAX_LEN) snakeLen++;
    score++;
    if (moveDelay > 70) moveDelay -= 6;
    placeFood();
  }
}

void draw() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Score: ");
  display.print(score);

  if (gameOver) {
    display.setCursor(34, 26);
    display.print("GAME OVER");
    display.setCursor(16, 44);
    display.print("R pour rejouer");
    display.display();
    return;
  }
  display.fillRect(foodX * CELL, HEADER + foodY * CELL, CELL, CELL, SSD1306_WHITE);
  for (int i = 0; i < snakeLen; i++)
    display.fillRect(snakeX[i] * CELL, HEADER + snakeY[i] * CELL, CELL, CELL, SSD1306_WHITE);
  display.display();
}

// Detection auto de l'ecran (2 sens de broches + 2 adresses)
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

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!initDisplayAuto()) {
    Serial.println("OLED introuvable - verifie l'alimentation et les fils.");
    while (true) delay(10);
  }

  randomSeed(esp_random());
  Serial.println("SNAKE ESP32-S3 - Z Q S D (ou fleches), R pour rejouer");
  resetGame();
}

void loop() {
  while (Serial.available() > 0) handleByte((char)Serial.read());

  if (!gameOver && millis() - lastMove >= moveDelay) {
    lastMove = millis();
    moveSnake();
    draw();
  }
}
