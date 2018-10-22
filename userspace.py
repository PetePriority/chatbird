from gtts import gTTS
from pydub import AudioSegment

from fcntl import ioctl

class _Getch:
    """Gets a single character from standard input.  Does not echo to the
screen. From http://code.activestate.com/recipes/134892/"""
    def __init__(self):
        self.impl = _GetchUnix()

    def __call__(self): return self.impl()


class _GetchUnix:
    def __init__(self):
        import tty, sys, termios # import termios now or else you'll get the Unix version on the Mac

    def __call__(self):
        import sys, tty, termios
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch

f = open("/dev/cb6", "wb")

try:
    inkey = _Getch()
    while (1):
        print("""
        1 - all off
        2 - LEDs on
        3 - Flap wings
        4 - Move beak
        5 - Tilt head
        6 - Reset wings and head
        s - Read text (English)
        g - Read text (German)

        q - Quit
        """)
        key = input("Command: ")
        # key = inkey()
        if key == "q": break
        if key == "s" or key == "g":
            text = input("Input text: ")
            tts = gTTS(text, lang='en' if key == "s" else 'de')
            tts.save('tmp.mp3')

            sound = AudioSegment.from_mp3('tmp.mp3')
            sound = sound.set_frame_rate(12000)
            # make quieter by amount in dB
            sound = sound - 15
            f.write(sound.raw_data)
            f.flush()
            continue
        print(key)
        key = key[0]
        # f.write(key)
        ioctl(f, 0, ord(key), False)
        f.flush()
except (KeyboardInterrupt, SystemExit):
    print("Exiting")

f.close()
