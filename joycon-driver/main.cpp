
#include <Windows.h>
#pragma comment(lib, "user32.lib")

#include <bitset>
#include <random>
#include <stdafx.h>
#include <string.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

#include <hidapi.h>

#include "public.h"
#include "vjoyinterface.h"

#include "packet.h"
#include "joycon.hpp"
#include "MouseController.hpp"
#include "tools.hpp"

// glm:
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>


#if defined(_WIN32)
#include <Windows.h>
#include <Lmcons.h>
#include <shlobj.h>
#endif


#pragma warning(disable:4996)

#define JOYCON_VENDOR 0x057e
#define JOYCON_L_BT 0x2006
#define JOYCON_R_BT 0x2007
#define PRO_CONTROLLER 0x2009
#define JOYCON_CHARGING_GRIP 0x200e
#define SERIAL_LEN 18
#define PI 3.14159265359
#define L_OR_R(lr) (lr == 1 ? 'L' : (lr == 2 ? 'R' : '?'))


std::vector<Joycon> joycons;
MouseController MC;
JOYSTICK_POSITION_V2 iReport; // The structure that holds the full position data
unsigned char buf[65];
int res = 0;


struct Settings {

	// Enabling this combines both JoyCons to a single vJoy Device(#1)
	// when combineJoyCons == false:
	// JoyCon(L) is mapped to vJoy Device #1
	// JoyCon(R) is mapped to vJoy Device #2
	// when combineJoyCons == true:
	// JoyCon(L) and JoyCon(R) are mapped to vJoy Device #1
	bool combineJoyCons = true;

	bool reverseX = false;// reverses joystick x (both sticks)
	bool reverseY = false;// reverses joystick y (both sticks)

	bool usingGrip = false;
	bool usingBluetooth = true;
	bool disconnect = false;

	// enables motion controls
	bool enableGyro = false;

	// gyroscope (mouse) sensitivity:
	float gyroSensitivityX = 150.0f;
	float gyroSensitivityY = 150.0f;


	// prefer the left joycon for gyro controls
	bool preferLeftJoyCon = false;

	// combo code to set key combination to disable gyroscope for quick turning in games. -1 to disable.
	int gyroscopeComboCode = 4;

	// toggles between two different toggle types
	// disabled = traditional toggle
	// enabled = while button(s) are held gyro is enabled
	bool quickToggleGyro = false;

	// inverts the above function
	bool invertQuickToggle = false;

	// for dolphin, mostly:
	bool dolphinPointerMode = false;

	// so that you don't rapidly toggle the gyro controls every frame:
	bool canToggleGyro = true;


	// enables 3D gyroscope visualizer
	bool gyroWindow = false;

	// plays a version of the mario theme by vibrating
	// the first JoyCon connected.
	bool marioTheme = false;

	// bool to restart the program
	bool restart = false;

	// auto start the program
	bool autoStart = false;

	// debug mode
	bool debugMode = false;

	// write debug to file:
	bool writeDebugToFile = false;

	// debug file:
	FILE* outputFile;

	// broadcast mode:
	bool broadcastMode = false;
	// where to connect:
	std::string host = "";
	// string to send:
	std::string controllerState = "";
	// write cast to file:
	bool writeCastToFile = false;

	// poll options:

	// force joycon to update when polled:
	bool forcePollUpdate = false;

	// times to poll per second per joycon:
	float pollsPerSec = 30.0f;

	// time to sleep (in ms) between polls:
	float timeToSleepMS = 4.0f;

	// version number
	std::string version = "1.07";

} settings;


struct Tracker {

	int var1 = 0;
	int var2 = 0;
	int counter1 = 0;

	float low_freq = 200.0f;
	float high_freq = 500.0f;

	float relX = 0;
	float relY = 0;

	float anglex = 0;
	float angley = 0;
	float anglez = 0;

	glm::fquat quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));

	// get current time
	//std::chrono::high_resolution_clock tNow;
	//std::chrono::steady_clock::time_point tPoll = std::chrono::high_resolution_clock::now();
	std::vector<std::chrono::steady_clock::time_point> tPolls;
	//Tracker(int value) : tPolls(100, std::chrono::high_resolution_clock::now()) {}
	//auto tSleepStart = std::chrono::high_resolution_clock::now();

	float previousPitch = 0;
} tracker;




void handle_input(Joycon* jc, uint8_t* packet, int len) {

	// bluetooth button pressed packet:
	if (packet[0] == 0x3F) {

		uint16_t old_buttons = jc->buttons;
		int8_t old_dstick = jc->dstick;

		jc->dstick = packet[3];
		// todo: get button states here aswell:
	}

	// input update packet:
	// 0x21 is just buttons, 0x30 includes gyro, 0x31 includes NFC (large packet size)
	if (packet[0] == 0x21 || packet[0] == 0x30 || packet[0] == 0x31) {

		// offset for usb or bluetooth data:
		/*int offset = settings.usingBluetooth ? 0 : 10;*/
		int offset = jc->bluetooth ? 0 : 10;

		uint8_t* btn_data = packet + offset + 3;

		// get button states:
		{
			uint16_t states = 0;
			uint16_t states2 = 0;

			// Left JoyCon:
			if (jc->left_right == 1) {
				states = (btn_data[1] << 8) | (btn_data[2] & 0xFF);
				// Right JoyCon:
			}
			else if (jc->left_right == 2) {
				states = (btn_data[1] << 8) | (btn_data[0] & 0xFF);
				// Pro Controller:
			}
			else if (jc->left_right == 3) {
				states = (btn_data[1] << 8) | (btn_data[2] & 0xFF);
				states2 = (btn_data[1] << 8) | (btn_data[0] & 0xFF);
			}

			jc->buttons = states;
			// Pro Controller:
			if (jc->left_right == 3) {
				jc->buttons2 = states2;

				// fix some non-sense the Pro Controller does
				// clear nth bit
				//num &= ~(1UL << n);
				jc->buttons &= ~(1UL << 9);
				jc->buttons &= ~(1UL << 10);
				jc->buttons &= ~(1UL << 12);
				jc->buttons &= ~(1UL << 14);

				jc->buttons2 &= ~(1UL << 8);
				jc->buttons2 &= ~(1UL << 11);
				jc->buttons2 &= ~(1UL << 13);
			}
		}

		// get stick data:
		uint8_t* stick_data = packet + offset;
		if (jc->left_right == 1) {
			stick_data += 6;
		}
		else if (jc->left_right == 2) {
			stick_data += 9;
		}

		uint16_t stick_x = stick_data[0] | ((stick_data[1] & 0xF) << 8);
		uint16_t stick_y = (stick_data[1] >> 4) | (stick_data[2] << 4);
		jc->stick.x = stick_x;
		jc->stick.y = stick_y;

		// use calibration data:
		jc->CalcAnalogStick();

		// pro controller:
		if (jc->left_right == 3) {
			stick_data += 6;
			uint16_t stick_x = stick_data[0] | ((stick_data[1] & 0xF) << 8);
			uint16_t stick_y = (stick_data[1] >> 4) | (stick_data[2] << 4);
			jc->stick.x = (int)(unsigned int)stick_x;
			jc->stick.y = (int)(unsigned int)stick_y;
			stick_data += 3;
			uint16_t stick_x2 = stick_data[0] | ((stick_data[1] & 0xF) << 8);
			uint16_t stick_y2 = (stick_data[1] >> 4) | (stick_data[2] << 4);
			jc->stick2.x = (int)(unsigned int)stick_x2;
			jc->stick2.y = (int)(unsigned int)stick_y2;

			// calibration data:
			jc->CalcAnalogStick();
		}

		jc->battery = (stick_data[1] & 0xF0) >> 4;
		//printf("JoyCon battery: %d\n", jc->battery);

		// Accelerometer:
		// Accelerometer data is absolute (m/s^2)
		{

			// get accelerometer X:
			jc->accel.x = (float)(uint16_to_int16(packet[13] | (packet[14] << 8) & 0xFF00)) * jc->acc_cal_coeff[0];

			// get accelerometer Y:
			jc->accel.y = (float)(uint16_to_int16(packet[15] | (packet[16] << 8) & 0xFF00)) * jc->acc_cal_coeff[1];

			// get accelerometer Z:
			jc->accel.z = (float)(uint16_to_int16(packet[17] | (packet[18] << 8) & 0xFF00)) * jc->acc_cal_coeff[2];
		}



		// Gyroscope:
		// Gyroscope data is relative (rads/s)
		{

			// get roll:
			jc->gyro.roll = (float)((uint16_to_int16(packet[19] | (packet[20] << 8) & 0xFF00)) - jc->sensor_cal[1][0]) * jc->gyro_cal_coeff[0];

			// get pitch:
			jc->gyro.pitch = (float)((uint16_to_int16(packet[21] | (packet[22] << 8) & 0xFF00)) - jc->sensor_cal[1][1]) * jc->gyro_cal_coeff[1];

			// get yaw:
			jc->gyro.yaw = (float)((uint16_to_int16(packet[23] | (packet[24] << 8) & 0xFF00)) - jc->sensor_cal[1][2]) * jc->gyro_cal_coeff[2];
		}

		// offsets:
		{
			jc->setGyroOffsets();

			jc->gyro.roll -= jc->gyro.offset.roll;
			jc->gyro.pitch -= jc->gyro.offset.pitch;
			jc->gyro.yaw -= jc->gyro.offset.yaw;

			//tracker.counter1++;
			//if (tracker.counter1 > 10) {
			//	tracker.counter1 = 0;
			//	printf("%.3f %.3f %.3f\n", abs(jc->gyro.roll), abs(jc->gyro.pitch), abs(jc->gyro.yaw));
			//}
		}


		//hex_dump(gyro_data, 20);

		if (jc->left_right == 1) {
			//hex_dump(gyro_data, 20);
			//hex_dump(packet+12, 20);
			//printf("x: %f, y: %f, z: %f\n", tracker.anglex, tracker.angley, tracker.anglez);
			//printf("%04x\n", jc->stick.x);
			//printf("%f\n", jc->stick.CalX);
			//printf("%d\n", jc->gyro.yaw);
			//printf("%02x\n", jc->gyro.roll);
			//printf("%04x\n", jc->gyro.yaw);
			//printf("%04x\n", jc->gyro.roll);
			//printf("%f\n", jc->gyro.roll);
			//printf("%d\n", accelXA);
			//printf("%d\n", jc->buttons);
			//printf("%.4f\n", jc->gyro.pitch);
			//printf("%04x\n", accelX);
			//printf("%02x %02x\n", rollA, rollB);
		}

	}






	// handle button combos:
	{

		// press up, down, left, right, L, ZL to restart:
		if (jc->left_right == 1) {
			//if (jc->buttons == 207) {
			//	settings.restart = true;
			//}

			// remove this, it's just for rumble testing
			//uint8_t hfa2 = 0x88;
			//uint16_t lfa2 = 0x804d;

			//tracker.low_freq = clamp(tracker.low_freq, 41.0f, 626.0f);
			//tracker.high_freq = clamp(tracker.high_freq, 82.0f, 1252.0f);
			//
			//// down:
			//if (jc->buttons == 1) {
			//	tracker.high_freq -= 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}
			//// down:
			//if (jc->buttons == 2) {
			//	tracker.high_freq += 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}

			//// left:
			//if (jc->buttons == 8) {
			//	tracker.low_freq -= 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}
			//// right:
			//if (jc->buttons == 4) {
			//	tracker.low_freq += 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}

			////printf("%i\n", jc->buttons);
			////printf("%f\n", tracker.frequency);
			//printf("%f %f\n", tracker.low_freq, tracker.high_freq);
		}


		// left:
		if (jc->left_right == 1) {
			jc->btns.down = (jc->buttons & (1 << 0)) ? 1 : 0;
			jc->btns.up = (jc->buttons & (1 << 1)) ? 1 : 0;
			jc->btns.right = (jc->buttons & (1 << 2)) ? 1 : 0;
			jc->btns.left = (jc->buttons & (1 << 3)) ? 1 : 0;
			jc->btns.sr = (jc->buttons & (1 << 4)) ? 1 : 0;
			jc->btns.sl = (jc->buttons & (1 << 5)) ? 1 : 0;
			jc->btns.l = (jc->buttons & (1 << 6)) ? 1 : 0;
			jc->btns.zl = (jc->buttons & (1 << 7)) ? 1 : 0;
			jc->btns.minus = (jc->buttons & (1 << 8)) ? 1 : 0;
			jc->btns.stick_button = (jc->buttons & (1 << 11)) ? 1 : 0;
			jc->btns.capture = (jc->buttons & (1 << 13)) ? 1 : 0;


			if (settings.debugMode) {
				printf("U: %d D: %d L: %d R: %d LL: %d ZL: %d SB: %d SL: %d SR: %d M: %d C: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.up, jc->btns.down, jc->btns.left, jc->btns.right, jc->btns.l, jc->btns.zl, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.minus, jc->btns.capture, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}
			if (settings.writeDebugToFile) {
				fprintf(settings.outputFile, "U: %d D: %d L: %d R: %d LL: %d ZL: %d SB: %d SL: %d SR: %d M: %d C: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.up, jc->btns.down, jc->btns.left, jc->btns.right, jc->btns.l, jc->btns.zl, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.minus, jc->btns.capture, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}
		}

		// right:
		if (jc->left_right == 2) {
			jc->btns.y = (jc->buttons & (1 << 0)) ? 1 : 0;
			jc->btns.x = (jc->buttons & (1 << 1)) ? 1 : 0;
			jc->btns.b = (jc->buttons & (1 << 2)) ? 1 : 0;
			jc->btns.a = (jc->buttons & (1 << 3)) ? 1 : 0;
			jc->btns.sr = (jc->buttons & (1 << 4)) ? 1 : 0;
			jc->btns.sl = (jc->buttons & (1 << 5)) ? 1 : 0;
			jc->btns.r = (jc->buttons & (1 << 6)) ? 1 : 0;
			jc->btns.zr = (jc->buttons & (1 << 7)) ? 1 : 0;
			jc->btns.plus = (jc->buttons & (1 << 9)) ? 1 : 0;
			jc->btns.stick_button = (jc->buttons & (1 << 10)) ? 1 : 0;
			jc->btns.home = (jc->buttons & (1 << 12)) ? 1 : 0;


			if (settings.debugMode) {
				printf("A: %d B: %d X: %d Y: %d RR: %d ZR: %d SB: %d SL: %d SR: %d P: %d H: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.a, jc->btns.b, jc->btns.x, jc->btns.y, jc->btns.r, jc->btns.zr, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.plus, jc->btns.home, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}
			if (settings.writeDebugToFile) {
				fprintf(settings.outputFile, "A: %d B: %d X: %d Y: %d RR: %d ZR: %d SB: %d SL: %d SR: %d P: %d H: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.a, jc->btns.b, jc->btns.x, jc->btns.y, jc->btns.r, jc->btns.zr, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.plus, jc->btns.home, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}
		}

		// pro controller:
		if (jc->left_right == 3) {

			// left:
			jc->btns.down = (jc->buttons & (1 << 0)) ? 1 : 0;
			jc->btns.up = (jc->buttons & (1 << 1)) ? 1 : 0;
			jc->btns.right = (jc->buttons & (1 << 2)) ? 1 : 0;
			jc->btns.left = (jc->buttons & (1 << 3)) ? 1 : 0;
			jc->btns.sr = (jc->buttons & (1 << 4)) ? 1 : 0;
			jc->btns.sl = (jc->buttons & (1 << 5)) ? 1 : 0;
			jc->btns.l = (jc->buttons & (1 << 6)) ? 1 : 0;
			jc->btns.zl = (jc->buttons & (1 << 7)) ? 1 : 0;
			jc->btns.minus = (jc->buttons & (1 << 8)) ? 1 : 0;
			jc->btns.stick_button = (jc->buttons & (1 << 11)) ? 1 : 0;
			jc->btns.capture = (jc->buttons & (1 << 13)) ? 1 : 0;

			// right:
			jc->btns.y = (jc->buttons2 & (1 << 0)) ? 1 : 0;
			jc->btns.x = (jc->buttons2 & (1 << 1)) ? 1 : 0;
			jc->btns.b = (jc->buttons2 & (1 << 2)) ? 1 : 0;
			jc->btns.a = (jc->buttons2 & (1 << 3)) ? 1 : 0;
			jc->btns.sr = (jc->buttons2 & (1 << 4)) ? 1 : 0;
			jc->btns.sl = (jc->buttons2 & (1 << 5)) ? 1 : 0;
			jc->btns.r = (jc->buttons2 & (1 << 6)) ? 1 : 0;
			jc->btns.zr = (jc->buttons2 & (1 << 7)) ? 1 : 0;
			jc->btns.plus = (jc->buttons2 & (1 << 9)) ? 1 : 0;
			jc->btns.stick_button2 = (jc->buttons2 & (1 << 10)) ? 1 : 0;
			jc->btns.home = (jc->buttons2 & (1 << 12)) ? 1 : 0;


			if (settings.debugMode) {

				printf("U: %d D: %d L: %d R: %d LL: %d ZL: %d SB: %d SL: %d SR: %d M: %d C: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.up, jc->btns.down, jc->btns.left, jc->btns.right, jc->btns.l, jc->btns.zl, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.minus, jc->btns.capture, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);

				printf("A: %d B: %d X: %d Y: %d RR: %d ZR: %d SB: %d SL: %d SR: %d P: %d H: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.a, jc->btns.b, jc->btns.x, jc->btns.y, jc->btns.r, jc->btns.zr, jc->btns.stick_button2, jc->btns.sl, jc->btns.sr, \
					jc->btns.plus, jc->btns.home, (jc->stick2.CalX + 1), (jc->stick2.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}

			if (settings.writeDebugToFile) {
				fprintf(settings.outputFile, "U: %d D: %d L: %d R: %d LL: %d ZL: %d SB: %d SL: %d SR: %d M: %d C: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.up, jc->btns.down, jc->btns.left, jc->btns.right, jc->btns.l, jc->btns.zl, jc->btns.stick_button, jc->btns.sl, jc->btns.sr, \
					jc->btns.minus, jc->btns.capture, (jc->stick.CalX + 1), (jc->stick.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);

				fprintf(settings.outputFile, "A: %d B: %d X: %d Y: %d RR: %d ZR: %d SB: %d SL: %d SR: %d P: %d H: %d SX: %.5f SY: %.5f GR: %06d GP: %06d GY: %06d\n", \
					jc->btns.a, jc->btns.b, jc->btns.x, jc->btns.y, jc->btns.r, jc->btns.zr, jc->btns.stick_button2, jc->btns.sl, jc->btns.sr, \
					jc->btns.plus, jc->btns.home, (jc->stick2.CalX + 1), (jc->stick2.CalY + 1), (int)jc->gyro.roll, (int)jc->gyro.pitch, (int)jc->gyro.yaw);
			}

		}

	}
}





int acquirevJoyDevice(int deviceID) {

	int stat;

	// Get the driver attributes (Vendor ID, Product ID, Version Number)
	if (!vJoyEnabled()) {
		printf("Function vJoyEnabled Failed - make sure that vJoy is installed and enabled\n");
		int dummy = getchar();
		stat = -2;
		throw;
	}
	else {
		//wprintf(L"Vendor: %s\nProduct :%s\nVersion Number:%s\n", static_cast<TCHAR *> (GetvJoyManufacturerString()), static_cast<TCHAR *>(GetvJoyProductString()), static_cast<TCHAR *>(GetvJoySerialNumberString()));
		//wprintf(L"Product :%s\n", static_cast<TCHAR *>(GetvJoyProductString()));
	};

	// Get the status of the vJoy device before trying to acquire it
	VjdStat status = GetVJDStatus(deviceID);

	switch (status) {
	case VJD_STAT_OWN:
		printf("vJoy device %d is already owned by this feeder\n", deviceID);
		break;
	case VJD_STAT_FREE:
		printf("vJoy device %d is free\n", deviceID);
		break;
	case VJD_STAT_BUSY:
		printf("vJoy device %d is already owned by another feeder\nCannot continue\n", deviceID);
		return -3;
	case VJD_STAT_MISS:
		printf("vJoy device %d is not installed or disabled\nCannot continue\n", deviceID);
		return -4;
	default:
		printf("vJoy device %d general error\nCannot continue\n", deviceID);
		return -1;
	};

	// Acquire the vJoy device
	if (!AcquireVJD(deviceID)) {
		printf("Failed to acquire vJoy device number %d.\n", deviceID);
		int dummy = getchar();
		stat = -1;
		throw;
	}
	else {
		printf("Acquired device number %d - OK\n", deviceID);
	}
}


void updatevJoyDevice2(Joycon* jc) {

	UINT DevID;

	PVOID pPositionMessage;
	UINT	IoCode = LOAD_POSITIONS;
	UINT	IoSize = sizeof(JOYSTICK_POSITION);
	// HID_DEVICE_ATTRIBUTES attrib;
	BYTE id = 1;
	UINT iInterface = 1;

	// Set destination vJoy device
	DevID = jc->vJoyNumber;
	id = (BYTE)DevID;
	iReport.bDevice = id;

	if (DevID == 0 && settings.debugMode) {
		printf("something went very wrong D:\n");
	}

	// Set Stick data

	int x = 0;
	int y = 0;
	//int z = 0;
	int z = 16384;
	int rx = 0;
	int ry = 0;
	int rz = 0;

	if (jc->deviceNumber == 0) {
		x = 16384 * (jc->stick.CalX);
		y = 16384 * (jc->stick.CalY);
	}
	else if (jc->deviceNumber == 1) {
		rx = 16384 * (jc->stick.CalX);
		ry = 16384 * (jc->stick.CalY);
	}
	// pro controller:
	if (jc->left_right == 3) {
		x = 16384 * (jc->stick.CalX);
		y = 16384 * (jc->stick.CalY);
		rx = 16384 * (jc->stick2.CalX);
		ry = 16384 * (jc->stick2.CalY);
	}


	x += 16384;
	y += 16384;
	rx += 16384;
	ry += 16384;
	//rz += 16384;

	if (settings.reverseX) {
		x = 32768 - x;
		rx = 32768 - rx;
	}
	if (settings.reverseY) {
		y = 32768 - y;
		ry = 32768 - ry;
	}


	if (jc->deviceNumber == 0) {
		iReport.wAxisX = x;
		iReport.wAxisY = y;
	}
	else if (jc->deviceNumber == 1) {
		iReport.wAxisXRot = rx;
		iReport.wAxisYRot = ry;
	}
	// pro controller:
	if (jc->left_right == 3) {
		// both sticks:
		iReport.wAxisX = x;
		iReport.wAxisY = y;
		iReport.wAxisXRot = rx;
		iReport.wAxisYRot = ry;
	}

	iReport.wAxisZ = z;// update z with 16384


	// prefer left joycon for gyroscope controls:
	int a = -1;
	int b = -1;
	if (settings.preferLeftJoyCon) {
		a = 1;
		b = 2;
	}
	else {
		a = 2;
		b = 1;
	}

	bool gyroComboCodePressed = false;



	// gyro / accelerometer data:
	if ((((jc->left_right == a) || (joycons.size() == 1 && jc->left_right == b) || (jc->left_right == 3)) && settings.combineJoyCons) || !settings.combineJoyCons) {

		int multiplier;


		// Gyroscope (roll, pitch, yaw):
		multiplier = 1000;




		// complementary filtered tracking
		// uses gyro + accelerometer

		// set to 0:
		tracker.quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));

		float gyroCoeff = 0.001;


		// x:
		float pitchDegreesAccel = glm::degrees((atan2(-jc->accel.x, -jc->accel.z) + PI));
		float pitchDegreesGyro = -jc->gyro.pitch * gyroCoeff;
		float pitch = 0;

		tracker.anglex += pitchDegreesGyro;
		if ((pitchDegreesAccel - tracker.anglex) > 180) {
			tracker.anglex += 360;
		}
		else if ((tracker.anglex - pitchDegreesAccel) > 180) {
			tracker.anglex -= 360;
		}
		tracker.anglex = (tracker.anglex * 0.98) + (pitchDegreesAccel * 0.02);
		pitch = tracker.anglex;

		glm::fquat delx = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0, 0.0, 0.0));
		tracker.quat = tracker.quat * delx;

		// y:
		float rollDegreesAccel = -glm::degrees((atan2(-jc->accel.y, -jc->accel.z) + PI));
		float rollDegreesGyro = -jc->gyro.roll * gyroCoeff;
		float roll = 0;

		tracker.angley += rollDegreesGyro;
		if ((rollDegreesAccel - tracker.angley) > 180) {
			tracker.angley += 360;
		}
		else if ((tracker.angley - rollDegreesAccel) > 180) {
			tracker.angley -= 360;
		}
		tracker.angley = (tracker.angley * 0.98) + (rollDegreesAccel * 0.02);
		//tracker.angley = -rollInDegreesAccel;
		roll = tracker.angley;


		glm::fquat dely = glm::angleAxis(glm::radians(roll), glm::vec3(0.0, 0.0, 1.0));
		tracker.quat = tracker.quat * dely;

		//printf("%f\n", roll);


		// z:
		float yawDegreesAccel = glm::degrees((atan2(-jc->accel.y, -jc->accel.x) + PI));
		float yawDegreesGyro = -jc->gyro.yaw * gyroCoeff;
		float yaw = 0;

		tracker.anglez += lowpassFilter(yawDegreesGyro, 0.5);
		//if ((yawInDegreesAccel - tracker.anglez) > 180) {
		//	tracker.anglez += 360;
		//} else if ((tracker.anglez - yawDegreesAccel) > 180) {
		//	tracker.anglez -= 360;
		//}
		//tracker.anglez = (tracker.anglez * 0.98) + (yawDegreesAccel * 0.02);
		yaw = tracker.anglez;


		glm::fquat delz = glm::angleAxis(glm::radians(-yaw), glm::vec3(0.0, 1.0, 0.0));
		tracker.quat = tracker.quat * delz;





		float relX2 = -jc->gyro.yaw * settings.gyroSensitivityX;
		float relY2 = jc->gyro.pitch * settings.gyroSensitivityY;

		relX2 /= 10;
		relY2 /= 10;

		//printf("%.3f %.3f %.3f\n", abs(jc->gyro.roll), abs(jc->gyro.pitch), abs(jc->gyro.yaw));
		//printf("%.2f %.2f\n", relX2, relY2);

		// check if combo keys are pressed:
		int comboCodeButtons = -1;
		if (jc->left_right != 3) {
			comboCodeButtons = jc->buttons;
		}
		else {
			comboCodeButtons = ((uint32_t)jc->buttons2 << 16) | jc->buttons;
		}

		//setGyroComboCodeText(comboCodeButtons);
		if (comboCodeButtons == settings.gyroscopeComboCode) {
			gyroComboCodePressed = true;
		}
		else {
			gyroComboCodePressed = false;
		}

		if (!gyroComboCodePressed) {
			settings.canToggleGyro = true;
		}

		if (settings.canToggleGyro && gyroComboCodePressed && !settings.quickToggleGyro) {
			settings.enableGyro = !settings.enableGyro;
			//gyroCheckBox->SetValue(settings.enableGyro);
			settings.canToggleGyro = false;
		}

		if (jc->left_right == 2) {
			relX2 *= -1;
			relY2 *= -1;
		}

		bool gyroActuallyOn = false;

		if (settings.enableGyro && settings.quickToggleGyro) {
			// check if combo keys are pressed:
			if (settings.invertQuickToggle) {
				if (!gyroComboCodePressed) {
					gyroActuallyOn = true;
				}
			}
			else {
				if (gyroComboCodePressed) {
					gyroActuallyOn = true;
				}
			}
		}

		if (settings.enableGyro && !settings.quickToggleGyro) {
			gyroActuallyOn = true;
		}

		float mult = settings.gyroSensitivityX * 10.0f;


		if (gyroActuallyOn) {
			MC.moveRel3(relX2, relY2);
		}

		if (settings.dolphinPointerMode) {
			iReport.wAxisZRot += (jc->gyro.roll * mult);
			iReport.wSlider += (jc->gyro.pitch * mult);
			iReport.wDial += (jc->gyro.yaw * mult);

			iReport.wAxisZRot = clamp(iReport.wAxisZRot, 0, 32678);
			iReport.wSlider = clamp(iReport.wSlider, 0, 32678);
			iReport.wDial = clamp(iReport.wDial, 0, 32678);
		}
		else {
			iReport.wAxisZRot = 16384 + (jc->gyro.roll * mult);
			iReport.wSlider = 16384 + (jc->gyro.pitch * mult);
			iReport.wDial = 16384 + (jc->gyro.yaw * mult);
		}
	}

	// Set button data
	// JoyCon(L) is the first 16 bits
	// JoyCon(R) is the last 16 bits

	long btns = 0;
	if (!settings.combineJoyCons) {
		btns = jc->buttons;
	}
	else {
		if (jc->left_right == 1) {
			// don't overwrite the other joycon
			btns = ((iReport.lButtons >> 16) << 16) | (jc->buttons);
		}
		else if (jc->left_right == 2) {
			btns = ((jc->buttons) << 16) | (createMask(0, 15) & iReport.lButtons);
		}
	}

	// Pro Controller:
	if (jc->left_right == 3) {
		uint32_t combined = ((uint32_t)jc->buttons2 << 16) | jc->buttons;
		btns = combined;
		//std::bitset<16> num1(jc->buttons);
		//std::bitset<16> num2(jc->buttons2);
		//std::cout << num1 << " " << num2 << std::endl;
	}

	iReport.lButtons = btns;

	// Send data to vJoy device
	pPositionMessage = (PVOID)(&iReport);
	if (!UpdateVJD(DevID, pPositionMessage)) {
		printf("Feeding vJoy device number %d failed - try to enable device then press enter\n", DevID);
		getchar();
		AcquireVJD(DevID);
	}
}





void parseSettings2() {

	//setupConsole("Debug");

	std::map<std::string, std::string> cfg = LoadConfig("config.txt");

	settings.combineJoyCons = (bool)stoi(cfg["combineJoyCons"]);
	settings.enableGyro = (bool)stoi(cfg["gyroControls"]);

	settings.gyroSensitivityX = stof(cfg["gyroSensitivityX"]);
	settings.gyroSensitivityY = stof(cfg["gyroSensitivityY"]);

	settings.gyroWindow = (bool)stoi(cfg["gyroWindow"]);
	settings.marioTheme = (bool)stoi(cfg["marioTheme"]);

	settings.reverseX = (bool)stoi(cfg["reverseX"]);
	settings.reverseY = (bool)stoi(cfg["reverseY"]);

	settings.preferLeftJoyCon = (bool)stoi(cfg["preferLeftJoyCon"]);
	settings.quickToggleGyro = (bool)stoi(cfg["quickToggleGyro"]);
	settings.invertQuickToggle = (bool)stoi(cfg["invertQuickToggle"]);

	settings.dolphinPointerMode = (bool)stoi(cfg["dolphinPointerMode"]);

	settings.gyroscopeComboCode = stoi(cfg["gyroscopeComboCode"]);

	settings.debugMode = (bool)stoi(cfg["debugMode"]);
	settings.writeDebugToFile = (bool)stoi(cfg["writeDebugToFile"]);

	settings.forcePollUpdate = (bool)stoi(cfg["forcePollUpdate"]);

	settings.broadcastMode = (bool)stoi(cfg["broadcastMode"]);
	settings.host = cfg["host"];
	settings.writeCastToFile = (bool)stoi(cfg["writeCastToFile"]);

	settings.autoStart = (bool)stoi(cfg["autoStart"]);

}

bool start();

void pollLoop() {

	// poll joycons:
	for (int i = 0; i < joycons.size(); ++i) {

		Joycon* jc = &joycons[i];

		// choose a random joycon to reduce bias / figure out the problem w/input lag:
		//Joycon *jc = &joycons[rand_range(0, joycons.size()-1)];

		if (!jc->handle) { continue; }


		if (settings.forcePollUpdate) {
			// set to be blocking:
			hid_set_nonblocking(jc->handle, 0);
		}
		else {
			// set to be non-blocking:
			hid_set_nonblocking(jc->handle, 1);
		}

		// get input:
		memset(buf, 0, 65);

		// get current time
		std::chrono::steady_clock::time_point tNow = std::chrono::high_resolution_clock::now();

		auto timeSincePoll = std::chrono::duration_cast<std::chrono::microseconds>(tNow - tracker.tPolls[i]);

		// time spent sleeping (0):
		double timeSincePollMS = timeSincePoll.count() / 1000.0;

		if (timeSincePollMS > (1000.0 / settings.pollsPerSec)) {
			jc->send_command(0x1E, buf, 0);
			tracker.tPolls[i] = std::chrono::high_resolution_clock::now();
		}


		//hid_read(jc->handle, buf, 0x40);
		hid_read_timeout(jc->handle, buf, 0x40, 20);

		// get rid of queue:
		// if we force the poll to wait then the queue will never clear and will just freeze:
		//if (!settings.forcePollUpdate) {
		//	while (hid_read(jc->handle, buf, 0x40) > 0) {};
		//}

		//for (int i = 0; i < 100; ++i) {
		//	hid_read(jc->handle, buf, 0x40);
		//}

		handle_input(jc, buf, 0x40);
	}

	// update vjoy:
	for (int i = 0; i < joycons.size(); ++i) {
		updatevJoyDevice2(&joycons[i]);
	}

	// sleep:
	if (settings.writeCastToFile) {
		veryAccurateSleep(settings.timeToSleepMS);
	}
	else {
		accurateSleep(settings.timeToSleepMS);
	}


	if (settings.broadcastMode && joycons.size() == 1) {
		Joycon* jc = &joycons[0];
		std::string newControllerState = "";


		if (jc->btns.up == 1 && jc->btns.left == 1) {
			newControllerState += "7";
		}
		else if (jc->btns.up && jc->btns.right == 1) {
			newControllerState += "1";
		}
		else if (jc->btns.down == 1 && jc->btns.left == 1) {
			newControllerState += "5";
		}
		else if (jc->btns.down == 1 && jc->btns.right == 1) {
			newControllerState += "3";
		}
		else if (jc->btns.up == 1) {
			newControllerState += "0";
		}
		else if (jc->btns.down == 1) {
			newControllerState += "4";
		}
		else if (jc->btns.left == 1) {
			newControllerState += "6";
		}
		else if (jc->btns.right == 1) {
			newControllerState += "2";
		}
		else {
			newControllerState += "8";
		}

		newControllerState += jc->btns.stick_button == 1 ? "1" : "0";
		newControllerState += jc->btns.l == 1 ? "1" : "0";
		newControllerState += jc->btns.zl == 1 ? "1" : "0";
		newControllerState += jc->btns.minus == 1 ? "1" : "0";
		newControllerState += jc->btns.capture == 1 ? "1" : "0";

		newControllerState += jc->btns.a == 1 ? "1" : "0";
		newControllerState += jc->btns.b == 1 ? "1" : "0";
		newControllerState += jc->btns.x == 1 ? "1" : "0";
		newControllerState += jc->btns.y == 1 ? "1" : "0";
		newControllerState += jc->btns.stick_button2 == 1 ? "1" : "0";
		newControllerState += jc->btns.r == 1 ? "1" : "0";
		newControllerState += jc->btns.zr == 1 ? "1" : "0";
		newControllerState += jc->btns.plus == 1 ? "1" : "0";
		newControllerState += jc->btns.home == 1 ? "1" : "0";

		int LX = ((jc->stick.CalX - 1.0) * 128) + 255;
		int LY = ((jc->stick.CalY - 1.0) * 128) + 255;
		int RX = ((jc->stick2.CalX - 1.0) * 128) + 255;
		int RY = ((jc->stick2.CalY - 1.0) * 128) + 255;

		newControllerState += " " + std::to_string(LX) + " " + std::to_string(LY) + " " + std::to_string(RX) + " " + std::to_string(RY);

		if (newControllerState != settings.controllerState) {
			/*settings.controllerState = newControllerState;
			printf("%s\n", newControllerState);
			myClient.socket()->emit("sendControllerState", newControllerState);*/
		}

		if (settings.writeCastToFile) {
			//std::string filename = "C:\\Users\\Matt\\Desktop\\commands.txt";
			//FILE* outputFile = fopen(filename.c_str(), "w");
			//fprintf(outputFile, "%s\n", newControllerState);
			//fclose(outputFile);
			fprintf(settings.outputFile, "%s\n", newControllerState);
		}
	}

	if (settings.broadcastMode && joycons.size() == 2) {
		Joycon* jcL;
		Joycon* jcR;

		if (joycons[0].left_right == 1) {
			jcL = &joycons[0];
			jcR = &joycons[1];
		}
		else {
			jcL = &joycons[1];
			jcR = &joycons[0];
		}

		std::string newControllerState = "";


		if (jcL->btns.up == 1 && jcL->btns.left == 1) {
			newControllerState += "7";
		}
		else if (jcL->btns.up && jcL->btns.right == 1) {
			newControllerState += "1";
		}
		else if (jcL->btns.down == 1 && jcL->btns.left == 1) {
			newControllerState += "5";
		}
		else if (jcL->btns.down == 1 && jcL->btns.right == 1) {
			newControllerState += "3";
		}
		else if (jcL->btns.up == 1) {
			newControllerState += "0";
		}
		else if (jcL->btns.down == 1) {
			newControllerState += "4";
		}
		else if (jcL->btns.left == 1) {
			newControllerState += "6";
		}
		else if (jcL->btns.right == 1) {
			newControllerState += "2";
		}
		else {
			newControllerState += "8";
		}

		newControllerState += jcL->btns.stick_button == 1 ? "1" : "0";
		newControllerState += jcL->btns.l == 1 ? "1" : "0";
		newControllerState += jcL->btns.zl == 1 ? "1" : "0";
		newControllerState += jcL->btns.minus == 1 ? "1" : "0";
		newControllerState += jcL->btns.capture == 1 ? "1" : "0";

		newControllerState += jcR->btns.a == 1 ? "1" : "0";
		newControllerState += jcR->btns.b == 1 ? "1" : "0";
		newControllerState += jcR->btns.x == 1 ? "1" : "0";
		newControllerState += jcR->btns.y == 1 ? "1" : "0";
		newControllerState += jcR->btns.stick_button2 == 1 ? "1" : "0";
		newControllerState += jcR->btns.r == 1 ? "1" : "0";
		newControllerState += jcR->btns.zr == 1 ? "1" : "0";
		newControllerState += jcR->btns.plus == 1 ? "1" : "0";
		newControllerState += jcR->btns.home == 1 ? "1" : "0";

		int LX = ((jcL->stick.CalX - 1.0) * 128) + 255;
		int LY = ((jcL->stick.CalY - 1.0) * 128) + 255;
		int RX = ((jcR->stick.CalX - 1.0) * 128) + 255;
		int RY = ((jcR->stick.CalY - 1.0) * 128) + 255;

		newControllerState += " " + std::to_string(LX) + " " + std::to_string(LY) + " " + std::to_string(RX) + " " + std::to_string(RY);

		if (newControllerState != settings.controllerState) {
			//settings.controllerState = newControllerState;
			//printf("%s\n", newControllerState);
			//myClient.socket()->emit("sendControllerState", newControllerState);
		}

		if (settings.writeCastToFile) {
			//std::string filename = "C:\\Users\\Matt\\Desktop\\commands.txt";
			//FILE* outputFile = fopen(filename.c_str(), "w");
			//fprintf(outputFile, "%s\n", newControllerState);
			//fclose(outputFile);
			fprintf(settings.outputFile, "%s\n", newControllerState);
		}
	}

	if (settings.restart) {
		settings.restart = false;
		start();
	}
}



bool start() {

	// set infinite reconnect attempts
	/*myClient.set_reconnect_attempts(999999999999);
	if (settings.broadcastMode) {
		myClient.connect(settings.host);
	}*/



	// get vJoy Device 1-8
	for (int i = 1; i < 9; ++i) {
		acquirevJoyDevice(i);
	}

	int read;	// number of bytes read
	int written;// number of bytes written
	const char* device_name;

	// Enumerate and print the HID devices on the system
	struct hid_device_info* devs, * cur_dev;

	res = hid_init();

	// hack:
	for (int i = 0; i < 100; ++i) {
		tracker.tPolls.push_back(std::chrono::high_resolution_clock::now());
	}


	if (settings.writeDebugToFile || settings.writeCastToFile) {

		// find a debug file to output to:
		int fileNumber = 0;
		std::string name = std::string("output-") + std::to_string(fileNumber) + std::string(".txt");
		while (exists_test0(name)) {
			fileNumber += 1;
			name = std::string("output-") + std::to_string(fileNumber) + std::string(".txt");
		}

		settings.outputFile = fopen(name.c_str(), "w");
	}


init_start:

	devs = hid_enumerate(JOYCON_VENDOR, 0x0);
	cur_dev = devs;
	while (cur_dev) {

		// identify by vendor:
		if (cur_dev->vendor_id == JOYCON_VENDOR) {

			// bluetooth, left / right joycon:
			if (cur_dev->product_id == JOYCON_L_BT || cur_dev->product_id == JOYCON_R_BT) {
				Joycon jc = Joycon(cur_dev);
				joycons.push_back(jc);
			}

			// pro controller:
			if (cur_dev->product_id == PRO_CONTROLLER) {
				Joycon jc = Joycon(cur_dev);
				joycons.push_back(jc);
			}

			// charging grip:
			//if (cur_dev->product_id == JOYCON_CHARGING_GRIP) {
			//	Joycon jc = Joycon(cur_dev);
			//	settings.usingBluetooth = false;
			//	settings.combineJoyCons = true;
			//	joycons.push_back(jc);
			//}

		}


		cur_dev = cur_dev->next;
	}

	hid_free_enumeration(devs);

	if (joycons.size() == 0) {
		printf("No JoyCon found!\r\n");
		return false;
	}

	// init joycons:
	if (settings.usingGrip) {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].init_usb();
		}
	}
	else {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].init_bt();
		}
	}

	if (settings.combineJoyCons) {
		int counter = 0;
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].vJoyNumber = (counter / 2) + 1;
			joycons[i].deviceNumber = (counter % 2 ? 1 : 0);
			counter++;
		}
	}
	else {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].vJoyNumber = i + 1;
			joycons[i].deviceNumber = 0;// left
		}
	}

	// initial poll to get battery data:
	pollLoop();
	for (int i = 0; i < joycons.size(); ++i) {
		printf("battery level: %u\n", joycons[i].battery);
	}

	// set lights:
	printf("setting LEDs...\n");
	for (int r = 0; r < 5; ++r) {
		for (int i = 0; i < joycons.size(); ++i) {
			Joycon* jc = &joycons[i];
			// Player LED Enable
			memset(buf, 0x00, 0x40);
			if (i == 0) {
				buf[0] = 0x0 | 0x0 | 0x0 | 0x1;		// solid 1
			}
			if (i == 1) {
				if (settings.combineJoyCons) {
					buf[0] = 0x0 | 0x0 | 0x0 | 0x1; // solid 1
				}
				else if (!settings.combineJoyCons) {
					buf[0] = 0x0 | 0x0 | 0x2 | 0x0; // solid 2
				}
			}
			//buf[0] = 0x80 | 0x40 | 0x2 | 0x1; // Flash top two, solid bottom two
			//buf[0] = 0x8 | 0x4 | 0x2 | 0x1; // All solid
			//buf[0] = 0x80 | 0x40 | 0x20 | 0x10; // All flashing
			//buf[0] = 0x80 | 0x00 | 0x20 | 0x10; // All flashing except 3rd light (off)
			jc->send_subcommand(0x01, 0x30, buf, 1);
		}
	}


	// give a small rumble to all joycons:
	printf("vibrating JoyCon(s).\n");
	for (int k = 0; k < 1; ++k) {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].rumble(100, 1);
			Sleep(20);
			joycons[i].rumble(10, 3);
		}
	}


	printf("Done.\n");
	return true;
}

void actuallyQuit() {

	for (int i = 1; i < 9; ++i) {
		RelinquishVJD(i);
	}

	for (int i = 0; i < joycons.size(); ++i) {
		buf[0] = 0x0; // disconnect
		joycons[i].send_subcommand(0x01, 0x06, buf, 1);
	}

	if (settings.usingGrip) {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].deinit_usb();
		}
	}
	// Finalize the hidapi library
	res = hid_exit();
}

bool running = true;

BOOL WINAPI SignalHandler(DWORD signal) {
	if (signal == CTRL_C_EVENT) {
		running = false;
		return TRUE;
	}

	return FALSE;
}

int main(int argc, char* argv[]) {
	//int wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow) {

	setupConsole("Debug");
	SetConsoleCtrlHandler(SignalHandler, true);
	
	parseSettings2();

	if (start()) {
		while (running) {
			pollLoop();
		}
	}

	actuallyQuit();
	return 0;
}
