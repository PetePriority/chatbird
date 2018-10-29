from time import sleep
from gtts import gTTS
from pydub import AudioSegment

from fcntl import ioctl
import logging

CHATBIRD_FLAP = ord('3')
CHATBIRD_BEAK = ord('4')
CHATBIRD_RESET = ord('6')
CHATBIRD_TILT = ord('5')
CHATBIRD_OFF = ord('1')

class Chatbird:
    def __init__(self, device):
        self.logger = logging.getLogger("Chatbird")
        self._device = device

    def __enter__(self):
        self._fd = open(self._device, 'wb', buffering=0)
        return ChatbirdInterface(self._fd)

    def __exit__(self, *args):
        self._fd.close()


class ChatbirdInterface:
    def __init__(self, fd):
        self._fd = fd

    def _send_command(self, command):
        ioctl(self._fd, 0, command, False)

    def _send_data(self, data):
        self._fd.write(data)

    def flap(self):
        self._send_command(CHATBIRD_FLAP)

    def beak(self):
        self._send_command(CHATBIRD_BEAK)

    def reset(self):
        self._send_command(CHATBIRD_RESET)

    def tilt(self):
        self._send_command(CHATBIRD_TILT)
        sleep(1)
        self.reset()

    def off(self):
        self._send_command(CHATBIRD_OFF)

    def say(self, text, lang='en'):
        tts = gTTS(text, lang=lang)
        # TODO: use write_to_fd instead
        tts.save('tmp.mp3')

        sound = AudioSegment.from_mp3('tmp.mp3')
        sound = sound.set_frame_rate(12000)
        # make quieter by amount in dB
        sound = sound - 15

        self.flap()
        sleep(2)
        self.beak()
        sleep(0.3)
        self._send_data(sound.raw_data)
        self.flap()
        sleep(1)
        self.off()
