# Zenith
An open source EOS controller based on [ETC Lighthack](https://github.com/ETCLabs/lighthack).
I built this to assist with lighting programming alongside an ETC Element 2, more specifically out of my hatred for solely using point-and-click to control my moving heads. Hopefully this project is of use to someone else too.

![Zenith MK1 Side](https://raw.githubusercontent.com/jamiejcole/zenith/main/Images/final_side.JPG)
![Zenith MK1 Front](https://raw.githubusercontent.com/jamiejcole/zenith/main/Images/final_front.JPG)

## Features
- 3 customisable rotary encoders (default to pan, tilt and zoom)
- 3 cherry MX blue switches to change the action of the corresponding encoder
- Go and stop/back cue buttons
- OLED screen showing current encoder settings and values

## Hardware
To build this, I used:
- 5x cherry MX blue switches
- 3x rotary encoders with pushbuttons
- 0.96 128x64 OLED screen
- Arduino UNO clone
- 3D printed housing and encoder wheels
- 4x M3 threaded inserts

## Notable challenges
Due to using an Arduino clone rather than a genuine one, the arduino had trouble communicating to the Element 2 via OSC over USB, which was a headache to troubleshoot. 
This was mitigated by exiting EOS and using a dodgy workaround to get to control panel and sideloading the Arduino serial drivers by using a USB drive. The comments section of [Nathan Butler's Lighthack](https://tinkering.home.blog/2019/02/19/lighthack-usb-encoder-wing-for-etc-element/) project helped significantly through the process of attempting to get this to work:

>> A couple of things to try:
– The usual driver for the knockoff arduino’s that I’ve used was available at https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers/all#drivers-if-you-need-them. I downloaded the windows driver from there, put it on a USB, logged into the desk in admin mode, and was able to open the USB drive and just install it.
– Similarly, you could try installing the Arduino software on the ETC desk too, which includes some drivers as part of it’s install.
– Whilst I’m guessing that you’re using the same USB cable at the desk as you did to upload the firmware to the arduino, just in case you are not, try a different USB cable. I have had some funny issues which turned out to be an intermittently faulty cable
Hope that helps!

>> Element 2 CH340 Driver install
> Any update on this? I have a win7 element 2 console and my arduino uno clone is not being recognised at all in diagnostics page. It's using the CH340 chipset, really struggling to get this to work. Is there any way i can install the drivers onto the console?

Update on this, was able to install the correct drivers by:
- loading into the console ECU,
- spamming shift to open the sticky keys menu
- clicking the hyperlink on the prompt to open control panel
- open device manager from control panel
- find the COM port for the Arduino 
- insert usb with extracted .zip folder for windows 7 CH340 drivers, downloaded from [this page](https://www.wch-ic.com/downloads/CH341SER_ZIP.html)
- click update drivers 
- select update from local file
- find extracted driver folder on USB
- install drivers
- exit out of all windows and launch EOS primary
- COM4 port is now recognised and can talk to Arduino via OSC over USB

## Wiring
Here was my 'circuit diagram' for the build.
![Circuit Diagram](https://raw.githubusercontent.com/jamiejcole/zenith/main/Images/Circuit%20Diagram.jpg)

And a mid-assembly pic.
![Assembly process](https://raw.githubusercontent.com/jamiejcole/zenith/main/Images/assembly.JPG)
