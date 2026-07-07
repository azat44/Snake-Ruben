"""
control_snake.py
Capture les fleches du clavier et les envoie a l'ESP32-S3 par le port serie.

Prerequis :
    pip install pyserial

Utilisation (Windows / PowerShell) :
    1. Ferme le moniteur serie de l'IDE Arduino (sinon le port est occupe).
    2. Adapte PORT ci-dessous au bon COMx (visible dans Tools > Port de l'IDE).
    3. py control_snake.py
    4. Clique dans la fenetre, puis appuie sur les fleches. Echap pour quitter.
"""

import sys
import serial

PORT = "COM4"      # <-- adapte au port de ta carte (COM3, COM5, /dev/ttyACM0, ...)
BAUD = 115200

# Sequences ANSI que l'ESP comprend (les memes qu'un vrai terminal)
UP    = b"\x1b[A"
DOWN  = b"\x1b[B"
RIGHT = b"\x1b[C"
LEFT  = b"\x1b[D"


def run_windows(ser):
    import msvcrt
    # Codes renvoyes par les touches fleches sous Windows (2e octet)
    arrows = {b"H": UP, b"P": DOWN, b"K": LEFT, b"M": RIGHT}
    print("Fleches pour jouer, R pour rejouer, Echap pour quitter.")
    while True:
        ch = msvcrt.getch()
        if ch in (b"\x00", b"\xe0"):          # prefixe des touches speciales
            code = msvcrt.getch()
            if code in arrows:
                ser.write(arrows[code])
        elif ch in (b"r", b"R"):
            ser.write(b"r")
        elif ch == b"\x1b":                    # Echap
            break


def run_unix(ser):
    import tty, termios
    print("Fleches pour jouer, R pour rejouer, Echap pour quitter.")
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while True:
            ch = sys.stdin.buffer.read(1)
            if ch == b"\x1b":                  # debut d'une sequence fleche
                seq = sys.stdin.buffer.read(2)
                if seq == b"[A": ser.write(UP)
                elif seq == b"[B": ser.write(DOWN)
                elif seq == b"[C": ser.write(RIGHT)
                elif seq == b"[D": ser.write(LEFT)
                elif seq == b"":               # Echap seul -> quitter
                    break
            elif ch in (b"r", b"R"):
                ser.write(b"r")
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"Impossible d'ouvrir {PORT} : {e}")
        print("Verifie le bon COMx et ferme le moniteur serie de l'IDE Arduino.")
        return
    print("Connecte !")
    try:
        if sys.platform.startswith("win"):
            run_windows(ser)
        else:
            run_unix(ser)
    finally:
        ser.close()
        print("\nDeconnecte.")


if __name__ == "__main__":
    main()
