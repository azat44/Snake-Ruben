  /*
  * SNAKE - ESP32-S3 + OLED SSD1306 (I2C) - controle clavier via WiFi + WebSocket
  *
  * L'ESP cree son propre reseau WiFi et sert une page web. La page ouvre une
  * connexion WebSocket vers l'ESP : quand tu appuies sur Z Q S D (ou les fleches)
  * la touche part instantanement par le WebSocket et le serpent bouge sur l'OLED.
  *
  * POUR JOUER :
  *   1. Connecte ton PC/telephone au WiFi :  SnakeESP   (mot de passe : snake1234)
  *   2. Ouvre un navigateur sur :  http://192.168.4.1
  *   3. Clique dans la page et joue avec Z Q S D. R = rejouer.
  *   (L'ecran OLED affiche ces infos au demarrage.)
  *
  * LIBRAIRIE A INSTALLER (Tools > Manage Libraries) :
  *   - "WebSockets" par Markus Sattler   (links2004/arduinoWebSockets)
  *   - Adafruit GFX Library
  *   - Adafruit SSD1306
  *   (WiFi et WebServer sont inclus dans le coeur ESP32)
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

  // Reseau WiFi cree par l'ESP
  const char* AP_SSID = "SnakeESP";
  const char* AP_PASS = "snake1234";        // 8 caracteres minimum

  WebServer        server(80);
  WebSocketsServer webSocket(81);           // le WebSocket ecoute sur le port 81

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

  // recoit une commande depuis le WebSocket : U D L R (directions) ou X (restart)
  void handleCommand(char c) {
    switch (c) {
      case 'U': setDir(UP);    break;
      case 'D': setDir(DOWN);  break;
      case 'L': setDir(LEFT);  break;
      case 'R': setDir(RIGHT); break;
      case 'X': if (gameOver) resetGame(); break;
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

  // ---- Page web servie a l'ouverture de http://192.168.4.1 ----
  const char PAGE[] PROGMEM = R"HTML(
  <!DOCTYPE html><html><head><meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
  <title>Snake ESP32</title>
  <style>
  body{font-family:sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:20px}
  h2{margin:10px}
  #etat{font-size:14px;color:#8f8;margin:6px}
  .pad{display:inline-grid;grid-template-columns:repeat(3,70px);gap:8px;margin-top:16px}
  button{height:70px;font-size:26px;border:0;border-radius:12px;background:#2a2a2a;color:#eee}
  button:active{background:#444}
  .hint{color:#888;font-size:14px;margin-top:14px}
  </style></head><body>
  <h2>Snake ESP32</h2>
  <div id="etat">Connexion...</div>
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
  });
  </script></body></html>
  )HTML";

  void handleRoot() { server.send_P(200, "text/html", PAGE); }

  // evenements WebSocket : on ne s'interesse qu'aux messages texte recus
  void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_TEXT && length > 0) {
      handleCommand((char)payload[0]);   // 1er caractere = la commande (U/D/L/R/X)
    }
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
    delay(4000);            // laisse le temps de lire les infos de connexion

    randomSeed(esp_random());
    resetGame();
  }

  void loop() {
    webSocket.loop();
    server.handleClient();

    if (!gameOver && millis() - lastMove >= moveDelay) {
      lastMove = millis();
      moveSnake();
      draw();
    }
  }
