# Chatbird-Bot USB Kernel Driver
The Chatbird-Bot driver is a Linux kernel driver for the Mitsumi PC Mascot.

![Chatbird](/res/chatbird.jpg)

I got this weird USB gadget in what must have been 2002. It could read your e-mails and annoyingly flap its wings while doing so. In 2018 I rediscovered this gadget in the attic and thought that this might make a good project to learn something about USB and Linux kernel drivers.

Unfortunately, it is nearly impossible to find anything about this device anymore on the internet. Thankfully, I still had kept the original driver CD containing drivers that only ran in Windows XP. After spinning up a VM and Wireshark an, I managed to record the USB request blocks to make the bird flap its wings, and how it transmitted the audio.

With the help of Matthias Vallentin's [blog post](http://matthias.vallentin.net/blog/2007/04/writing-a-linux-kernel-driver-for-an-unknown-usb-device/) about writing a linux kernel driver for an USB missile launcher, I managed to write this driver.

The driver exposes the device as a char device. Using `ioctl` it is possible to send it commands, for instance, to make it flap its wings and to move its beak. Through a lucky guess, I figured out that to use it as a speaker, it is enough to pipe audio formatted as PCM to the char device.

I also wrote a simple bot for Telegram that will read out messages sent to the device, while flapping its wings. The text is transformed into audio using Google's `gTTS` module. If the message ends with a question mark, the bird will also tilt its head after reading the message aloud. Perfect to annoy your co-workers! :)

# Usage

Compile (make sure to have your kernel headers installed):
```
make
```

Load the driver using the provided script:
```
./load_and_reset.sh
```

Loading the driver could possibly be improved using `udev`-rules.

# License
This kernel module comes with a GPL license.

