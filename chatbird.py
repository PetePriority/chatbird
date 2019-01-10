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
        self._fd.flush()

    def _send_data(self, data):
        while len(data) > 0:
            bytes_written = self._fd.write(data)
            data = data[bytes_written:]

    def flap(self):
        self._send_command(CHATBIRD_FLAP)

    def beak(self):
        self._send_command(CHATBIRD_BEAK)

    def reset(self):
        self._send_command(CHATBIRD_RESET)

    def tilt(self):
        self._send_command(CHATBIRD_TILT)
        sleep(.5)
        self.reset()
        sleep(1)

    def off(self):
        self._send_command(CHATBIRD_OFF)

    def say(self, text, lang='en'):
        text_segments = text.split('?')
        text_segments = [x + "?" for x in text_segments[:-1]] + [text_segments[-1]]
        spoken_segments = []

        for segment in text_segments:
            if segment.strip() != "":
                tts = gTTS(segment, lang=lang)
                # TODO: use write_to_fd instead
                tts.save('tmp.mp3')
                sound = AudioSegment.from_mp3('tmp.mp3')
                sound = sound.set_frame_rate(12000)
                # make quieter by amount in dB
                sound = sound - 15
            else:
                sound = AudioSegment.empty()
            spoken_segments.append(sound)


        self.flap()
        sleep(2)
        self.beak()
        sleep(0.3)
        # pop first segment from list
        sound = spoken_segments.pop(0)
        self._send_data(sound.raw_data)

        # If text contained a '?', the list won't be empty
        # Tilt head before each of the remaining segments
        for sound in spoken_segments:
            self.tilt()
            self.beak()
            self._send_data(sound.raw_data)

        self.flap()
        sleep(1)
        self.off()
