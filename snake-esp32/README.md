# Snake ESP32-S3

Jeu Snake sur ESP32-S3 avec ecran OLED SSD1306 (I2C), controle par les
**fleches du clavier** via un petit script Python. Compilation verifiee
automatiquement par GitHub Actions.

## Structure du depot

```
snake-esp32/
├── snake_esp32_usb/
│   └── snake_esp32_usb.ino   # le jeu (a televerser sur la carte)
├── control_snake.py          # envoie les fleches a la carte
├── .github/workflows/build.yml  # CI : compile le sketch a chaque push
└── README.md
```

> Le fichier `.ino` **doit** rester dans un dossier portant le meme nom
> (`snake_esp32_usb/snake_esp32_usb.ino`) — c'est une regle de l'IDE Arduino
> et la CI en depend aussi.

## Cablage OLED

| OLED | ESP32-S3 |
|------|----------|
| VCC  | 3V3      |
| GND  | GND      |
| SDA  | GPIO 8   |
| SCL  | GPIO 9   |

## Televerser

1. IDE Arduino : `Tools > USB CDC On Boot` → **Enabled** (indispensable sur le S3).
2. `Tools > Board` → ESP32S3 Dev Module ; `Tools > Port` → le bon COMx.
3. Librairies : Adafruit GFX Library + Adafruit SSD1306.
4. Ouvre `snake_esp32_usb.ino` et televerse (flèche →).
5. Si l'ecran reste noir : change `0x3C` en `0x3D` dans le code.

## Jouer

1. **Ferme le moniteur serie de l'IDE** (sinon le port est occupe).
2. `pip install pyserial`
3. Dans `control_snake.py`, mets le bon `PORT` (COMx).
4. `py control_snake.py`
5. Clique dans la fenetre puis utilise les **fleches**. `R` rejoue, `Echap` quitte.

ZQSD et WASD marchent aussi en secours.

## CI/CD

Le workflow `.github/workflows/build.yml` s'execute a chaque `push` et
`pull_request` : il installe le coeur ESP32 + les librairies Adafruit et
**compile le sketch pour l'ESP32-S3**. Si le code ne compile pas, le job
echoue (croix rouge sur GitHub) ; s'il compile, coche verte.

> Note : un runner GitHub (dans le cloud) ne peut pas flasher ta carte
> physique. Pour de l'Arduino, le CI verifie donc la **compilation**, ce qui
> attrape la grande majorite des erreurs avant meme de brancher la carte.

### Mettre en place

```bash
cd snake-esp32
git init
git add .
git commit -m "Snake ESP32-S3 + CI"
git branch -M main
git remote add origin https://github.com/<ton-user>/<ton-repo>.git
git push -u origin main
```

Va ensuite dans l'onglet **Actions** de ton depot GitHub pour voir le build.
