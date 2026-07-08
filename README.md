# Snake ESP32-S3

Jeu Snake sur ESP32-S3 avec ecran OLED SSD1306 (I2C), controle au clavier
(**Z Q S D**) depuis une page web via **WebSocket** en WiFi. Compilation
verifiee automatiquement par GitHub Actions (CI).

## Structure du depot

```
snake-esp32/
├── snake_esp32_usb/
│   └── snake_esp32_usb.ino       # le jeu (a televerser sur la carte)
├── .github/workflows/build.yml   # CI : compile le sketch a chaque push
└── README.md
```

> Le fichier `.ino` doit rester dans un dossier portant le meme nom
> (`snake_esp32_usb/snake_esp32_usb.ino`) — c'est une regle de l'IDE Arduino,
> et la CI en depend aussi.

## Cablage OLED

| OLED | ESP32-S3 |
|------|----------|
| VCC  | 3V3      |
| GND  | GND      |
| SDA  | GPIO 17  |
| SCL  | GPIO 18  |

> Le code detecte automatiquement le sens des broches (17/18 ou 18/17) et
> l'adresse I2C (0x3C ou 0x3D), donc aucun reglage manuel n'est necessaire.

## Librairies a installer (IDE Arduino > Tools > Manage Libraries)

- **WebSockets** par Markus Sattler (*links2004/arduinoWebSockets*)
- **Adafruit GFX Library**
- **Adafruit SSD1306**

(`WiFi` et `WebServer` sont inclus dans le coeur ESP32.)

## Televerser

1. `Tools > Board` → ESP32S3 Dev Module ; `Tools > Port` → le bon COMx.
2. Ouvre `snake_esp32_usb.ino` et televerse (flèche →).
3. Au demarrage, l'OLED affiche les infos de connexion pendant 4 secondes.

## Jouer

1. Sur ton PC ou telephone, connecte-toi au reseau WiFi cree par l'ESP :
   **SnakeESP** (mot de passe : **snake1234**).
2. Ouvre un navigateur sur **http://192.168.4.1**.
3. La page affiche « Connecte » → joue avec **Z Q S D** (ou les fleches).
   **R** = rejouer. Les touches sont envoyees en direct par WebSocket, sans
   appuyer sur Entree.

## CI (GitHub Actions)

Le workflow `.github/workflows/build.yml` s'execute a chaque `push` et
`pull_request` : il installe le coeur ESP32 + les librairies (Adafruit GFX,
Adafruit SSD1306, WebSockets) et **compile le sketch pour l'ESP32-S3**. Si le
code ne compile pas, le job echoue (croix rouge) ; sinon, coche verte.

> Note : un runner GitHub (dans le cloud) ne peut pas flasher une carte
> physique. Pour de l'Arduino, la CI verifie donc la **compilation**, ce qui
> attrape la grande majorite des erreurs avant meme de brancher la carte.

## Mettre en place le depot

```bash
cd snake-esp32
git init
git add .
git commit -m "Snake ESP32-S3 WebSocket + CI"
git branch -M main
git remote add origin https://github.com/<ton-user>/<ton-repo>.git
git push -u origin main
```

Va ensuite dans l'onglet **Actions** de ton depot GitHub pour voir le build.
