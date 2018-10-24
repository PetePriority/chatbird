from time import sleep
from gtts import gTTS
from pydub import AudioSegment

from fcntl import ioctl
import logging

from telegram.ext.filters import Filters
from telegram.ext import Updater, CommandHandler, MessageHandler

from chatbird import Chatbird
from secrets import TOKEN

CHATBIRD_DEVICE = '/dev/cb1'

logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                             level=logging.INFO)

updater = Updater(token=TOKEN)
dispatcher = updater.dispatcher

def say(bot, update):
    text = update.message.text
    print("Message received {}".format(text))

    with Chatbird(CHATBIRD_DEVICE) as cb:
        cb.say(text, 'de')

message_handler = MessageHandler(Filters.text, say)
#say_handler = CommandHandler('say', say)
dispatcher.add_handler(message_handler)

updater.start_polling()
