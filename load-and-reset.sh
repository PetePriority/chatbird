#!/bin/bash

rmmod chatbird; insmod chatbird.ko; ./usbreset /dev/bus/usb/003/006;
sleep 1; 
ln -s /dev/cb2 /dev/cb1
chmod 777 /dev/cb*
