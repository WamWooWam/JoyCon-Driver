# JoyCon-Driver
A vJoy feeder / Driver for the Nintendo Switch JoyCons and Pro Controller on Windows with analog stick support and motion controls

## How to use
1. Install vJoy, here: http://vjoystick.sourceforge.net/site/

2. Setup your vJoy Devices to look like this (search for configure vJoy in Windows search):
    * ![Imgur](http://i.imgur.com/nXQDFPK.png)
    * Add a device for every controller you have, so if you have 4 JoyCons and 1 Pro Controller, enable 5 devices

3. Pair the JoyCon(s) / Pro Controller(s) to your PC

4. Run the Application, if it doesn't detect your JoyCon(s) / Pro Controller, make sure they are fully paired / connected and restart the program.

5. Use `Ctrl`+`C` to exit

## Building
1. Download the [vJoy SDK](https://github.com/shauleiz/vJoy/tree/master/SDK)

2. Download [hidapi-win](https://github.com/libusb/hidapi/releases/)

3. Copy the downloaded files to the `libs/` directory so they look like this: 
```
libs
├───hidapi-win
│   ├───x64
│   │       hidapi.dll
│   │       hidapi.lib
│   │
│   └───x86
│           hidapi.dll
│           hidapi.lib
│
└───vjoy
    ├───x64
    │       vJoyInterface.dll
    │       vJoyInterface.lib
    │
    └───x86
            vJoyInterface.dll
            vJoyInterface.lib
```

4. Open `joycon.sln` in Visual Studio (2019 or 2017), retarget if needed and build. 


## Settings and features (some may not work anymore)
* Combine JoyCons
	* Combines a pair of JoyCons into a single vJoy device
* Reverse Stick X/Y
	* Reverses the X/Y direction(s) for both sticks
* Gyro Controls
	* Enables controlling the mouse with a JoyCon like a WiiMote
* Prefer Left JoyCon
	* By default, the right JoyCon is used (if found), this forces the program to use the left JoyCon (if found)
* Gyro Controls Sensitivity X/Y
	* Controls the sensitivity -> higher = more sensitive
	* The X sensitivity also controls the gyro sensitivity for Rz/sl0/sl1 in vJoy
* Gyroscope Combo Code
	* A number that tells the program which button or set of buttons to use to toggle gyro controls
	* To figure out what number to put in the config, look at the Gyro Combo Code when you press your desired keycombo
* Quick Toggle Gyro
	* Changes the behavior of the Gyro toggle from a standard switch, to a "always off unless keycombo is pressed" mode
* Invert Quick Toggle
	* Changes the behavior of the quick toggle from always off unless keycombo is pressed to always on unless keycombo is pressed
* Gyro Window
	* Opens up a visualizer for the JoyCon's gyroscope
* Dolphin Mode
	* Makes it so that the Rz/sl0/sl1 sliders in vJoy don't reset back to 0 when the JoyCon stops moving
* Debug Mode
	* Prints debug info to the console
* Write Debug to File
	* Writes the debug info to a file
* Force Poll Update
	* Don't use this, probably

## Important Notes
* The JoyCons need to be re-paired anytime after they've reconnected to the swit

## Thanks
* Thanks to everyone at: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/
* @fossephate for the original project!