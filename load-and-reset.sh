#!/bin/bash

rmmod chatbird; insmod chatbird.ko; ./usbreset /dev/bus/usb/004/002; sleep 1; chmod 777 /dev/cb6
