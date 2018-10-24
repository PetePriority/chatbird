from time import sleep
from gtts import gTTS
from pydub import AudioSegment

from fcntl import ioctl
import logging

from telegram.ext.filters import Filters
from telegram.ext import Updater, CommandHandler, MessageHandler

from secrets import TOKEN

logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                             level=logging.INFO)


CHATBIRD_FLAP = ord('3')
CHATBIRD_BEAK = ord('4')
CHATBIRD_RESET = ord('6')
CHATBIRD_TILT = ord('5')
CHATBIRD_OFF = ord('1')

updater = Updater(token=TOKEN)
dispatcher = updater.dispatcher

def exec_command(f, command):
    ioctl(f, 0, command, False)
    f.flush()

def exec_speak(f, data):
    f.write(data)
    f.flush()

def say(bot, update):
    text = update.message.text
    print("Message received {}".format(text))

    tts = gTTS(text, lang='de')
    tts.save('tmp.mp3')

    sound = AudioSegment.from_mp3('tmp.mp3')
    sound = sound.set_frame_rate(12000)
    # make quieter by amount in dB
    sound = sound - 15

    f = open("/dev/cb1", "wb")
    exec_command(f, CHATBIRD_FLAP)
    sleep(2)
    exec_command(f, CHATBIRD_BEAK)
    sleep(0.3)
    exec_speak(f, sound.raw_data)
    #sleep(sound.duration_seconds + 1)
    exec_command(f, CHATBIRD_FLAP)
    sleep(1)
    exec_command(f, CHATBIRD_OFF)
    f.close()

message_handler = MessageHandler(Filters.text, say)
#say_handler = CommandHandler('say', say)
dispatcher.add_handler(message_handler)

updater.start_polling()

# f = open("/dev/cb1", "wb")

# try:
#     inkey = _Getch()
#     while (1):
#         print("""
#         1 - all off
#         2 - LEDs on
#         3 - Flap wings
#         4 - Move beak
#         5 - Tilt head
#         6 - Reset wings and head
#         s - Read text (English)
#         g - Read text (German)

#         q - Quit
#         """)
#         key = input("Command: ")
#         # key = inkey()
#         if key == "q": break
#         if key == "s" or key == "g":
#             text = input("Input text: ")
#             tts = gTTS(text, lang='en' if key == "s" else 'de')
#             tts.save('tmp.mp3')

#             sound = AudioSegment.from_mp3('tmp.mp3')
#             sound = sound.set_frame_rate(12000)
#             # make quieter by amount in dB
#             sound = sound - 15
#             f.write(sound.raw_data)
#             f.flush()
#             continue
#         print(key)
#         key = key[0]
#         # f.write(key)
#         ioctl(f, 0, ord(key), False)
#         f.flush()
# except (KeyboardInterrupt, SystemExit):
#     print("Exiting")

# f.close()
