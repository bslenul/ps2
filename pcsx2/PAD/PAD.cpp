﻿/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <array>
#include <libretro.h>

#include "PAD.h"

#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/SettingsInterface.h"
#include "common/Pcsx2Defs.h"

#include "Frontend/InputManager.h"
#include "../Host.h"
#include "../Sio.h"

#define MODE_DIGITAL	0x41
#define MODE_ANALOG	0x73
#define MODE_DS2_NATIVE 0x79

#if 1
#define IsDualshock2() true
#else
#define IsDualShock2() (config.padConfigs[query.port][query.slot].type == Dualshock2Pad || (config.padConfigs[query.port][query.slot].type == GuitarPad && config.GH2))
#endif

/* Total number of pad ports, across both multitaps. */
#define NUM_CONTROLLER_PORTS 8

#define DEFAULT_MOTOR_SCALE 1.0f

#define TEST_BIT(value, bit) ((value) & (1 << (bit)))

enum PadCommands
{
	CMD_SET_VREF_PARAM        = 0x40,
	CMD_QUERY_DS2_ANALOG_MODE = 0x41,
	CMD_READ_DATA_AND_VIBRATE = 0x42,
	CMD_CONFIG_MODE           = 0x43,
	CMD_SET_MODE_AND_LOCK     = 0x44,
	CMD_QUERY_MODEL_AND_MODE  = 0x45,
	CMD_QUERY_ACT             = 0x46, /* ?? */
	CMD_QUERY_COMB            = 0x47, /* ?? */
	CMD_QUERY_MODE            = 0x4C, /* QUERY_MODE ?? */
	CMD_VIBRATION_TOGGLE      = 0x4D,
	CMD_SET_DS2_NATIVE_MODE   = 0x4F  /* SET_DS2_NATIVE_MODE */
};

enum gamePadValues
{
	PAD_UP = 0,   //  0  - Directional pad ↑
	PAD_RIGHT,    //  1  - Directional pad →
	PAD_DOWN,     //  2  - Directional pad ↓
	PAD_LEFT,     //  3  - Directional pad ←
	PAD_TRIANGLE, //  4  - Triangle button ▲
	PAD_CIRCLE,   //  5  - Circle button ●
	PAD_CROSS,    //  6  - Cross button ✖
	PAD_SQUARE,   //  7  - Square button ■
	PAD_SELECT,   //  8  - Select button
	PAD_START,    //  9  - Start button
	PAD_L1,       // 10  - L1 button
	PAD_L2,       // 11  - L2 button
	PAD_R1,       // 12  - R1 button
	PAD_R2,       // 13  - R2 button
	PAD_L3,       // 14  - Left joystick button (L3)
	PAD_R3,       // 15  - Right joystick button (R3)
	PAD_ANALOG,   // 16  - Analog mode toggle
	PAD_PRESSURE, // 17  - Pressure modifier
	PAD_L_UP,     // 18  - Left joystick (Up) ↑
	PAD_L_RIGHT,  // 19  - Left joystick (Right) →
	PAD_L_DOWN,   // 20  - Left joystick (Down) ↓
	PAD_L_LEFT,   // 21  - Left joystick (Left) ←
	PAD_R_UP,     // 22  - Right joystick (Up) ↑
	PAD_R_RIGHT,  // 23  - Right joystick (Right) →
	PAD_R_DOWN,   // 24  - Right joystick (Down) ↓
	PAD_R_LEFT,   // 25  - Right joystick (Left) ←
	MAX_KEYS
};

// Full state to manage save state
struct PadFullFreezeData
{
	char format[8];
	// active slot for port
	u8 slot[2];
	PadFreezeData padData[2][4];
	QueryInfo query;
};

struct KeyStatus
{
	ControllerType m_type[NUM_CONTROLLER_PORTS] = {};
	float m_vibration_scale[NUM_CONTROLLER_PORTS][2];
};

static const InputBindingInfo s_dualshock2_binds[] = {
	{"Up", "D-Pad Up", InputBindingInfo::Type::Button, PAD_UP, GenericInputBinding::DPadUp},
	{"Right", "D-Pad Right", InputBindingInfo::Type::Button, PAD_RIGHT, GenericInputBinding::DPadRight},
	{"Down", "D-Pad Down", InputBindingInfo::Type::Button, PAD_DOWN, GenericInputBinding::DPadDown},
	{"Left", "D-Pad Left", InputBindingInfo::Type::Button, PAD_LEFT, GenericInputBinding::DPadLeft},
	{"Triangle", "Triangle", InputBindingInfo::Type::Button, PAD_TRIANGLE, GenericInputBinding::Triangle},
	{"Circle", "Circle", InputBindingInfo::Type::Button, PAD_CIRCLE, GenericInputBinding::Circle},
	{"Cross", "Cross", InputBindingInfo::Type::Button, PAD_CROSS, GenericInputBinding::Cross},
	{"Square", "Square", InputBindingInfo::Type::Button, PAD_SQUARE, GenericInputBinding::Square},
	{"Select", "Select", InputBindingInfo::Type::Button, PAD_SELECT, GenericInputBinding::Select},
	{"Start", "Start", InputBindingInfo::Type::Button, PAD_START, GenericInputBinding::Start},
	{"L1", "L1 (Left Bumper)", InputBindingInfo::Type::Button, PAD_L1, GenericInputBinding::L1},
	{"L2", "L2 (Left Trigger)", InputBindingInfo::Type::HalfAxis, PAD_L2, GenericInputBinding::L2},
	{"R1", "R1 (Right Bumper)", InputBindingInfo::Type::Button, PAD_R1, GenericInputBinding::R1},
	{"R2", "R2 (Right Trigger)", InputBindingInfo::Type::HalfAxis, PAD_R2, GenericInputBinding::R2},
	{"L3", "L3 (Left Stick Button)", InputBindingInfo::Type::Button, PAD_L3, GenericInputBinding::L3},
	{"R3", "R3 (Right Stick Button)", InputBindingInfo::Type::Button, PAD_R3, GenericInputBinding::R3},
	{"Analog", "Analog Toggle", InputBindingInfo::Type::Button, PAD_ANALOG, GenericInputBinding::System},
	{"Pressure", "Apply Pressure", InputBindingInfo::Type::Button, PAD_PRESSURE, GenericInputBinding::Unknown},
	{"LUp", "Left Stick Up", InputBindingInfo::Type::HalfAxis, PAD_L_UP, GenericInputBinding::LeftStickUp},
	{"LRight", "Left Stick Right", InputBindingInfo::Type::HalfAxis, PAD_L_RIGHT, GenericInputBinding::LeftStickRight},
	{"LDown", "Left Stick Down", InputBindingInfo::Type::HalfAxis, PAD_L_DOWN, GenericInputBinding::LeftStickDown},
	{"LLeft", "Left Stick Left", InputBindingInfo::Type::HalfAxis, PAD_L_LEFT, GenericInputBinding::LeftStickLeft},
	{"RUp", "Right Stick Up", InputBindingInfo::Type::HalfAxis, PAD_R_UP, GenericInputBinding::RightStickUp},
	{"RRight", "Right Stick Right", InputBindingInfo::Type::HalfAxis, PAD_R_RIGHT, GenericInputBinding::RightStickRight},
	{"RDown", "Right Stick Down", InputBindingInfo::Type::HalfAxis, PAD_R_DOWN, GenericInputBinding::RightStickDown},
	{"RLeft", "Right Stick Left", InputBindingInfo::Type::HalfAxis, PAD_R_LEFT, GenericInputBinding::RightStickLeft},
	{"LargeMotor", "Large (Low Frequency) Motor", InputBindingInfo::Type::Motor, 0, GenericInputBinding::LargeMotor},
	{"SmallMotor", "Small (High Frequency) Motor", InputBindingInfo::Type::Motor, 0, GenericInputBinding::SmallMotor},
};

static const PAD::ControllerInfo s_controller_info[] = {
	{NotConnected, "None", nullptr, 0, NoVibration},
	{DualShock2, "DualShock2", s_dualshock2_binds, std::size(s_dualshock2_binds), LargeSmallMotors},
};

static KeyStatus g_key_status;

// Typical packet response on the bus
static const u8 ConfigExit[7]    = {0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const u8 noclue[7]        = {0x5A, 0x00, 0x00, 0x02, 0x00, 0x00, 0x5A};
static const u8 setMode[7]       = {0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const u8 queryModelDS2[7] = {0x5A, 0x03, 0x02, 0x00, 0x02, 0x01, 0x00};
static const u8 queryModelDS1[7] = {0x5A, 0x01, 0x02, 0x00, 0x02, 0x01, 0x00};
static const u8 queryComb[7]     = {0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
static const u8 queryMode[7]     = {0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const u8 setNativeMode[7] = {0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A};

static u8 queryMaskMode[7]       = {0x5A, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x5A};

static const u8 queryAct[2][7]   = {
	{0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A},
	{0x5A, 0x00, 0x00, 0x01, 0x01, 0x01, 0x14}};

static QueryInfo query;
static Pad pads[2][4];
static int slots[2] = {0, 0};

extern retro_environment_t environ_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
struct retro_rumble_interface rumble;

static struct retro_input_descriptor desc[] = {
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
	{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
	{ 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
	{ 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
	{ 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
	{ 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

	{0},
};

static u32 button_mask[2];
static int pad_lx[2];
static int pad_ly[2];
static int pad_rx[2];
static int pad_ry[2];
static int pad_type[2] = { -1, -1 };
extern float pad_axis_scale[2];

static int keymap[] =
{
	RETRO_DEVICE_ID_JOYPAD_L2,     // PAD_L2
	RETRO_DEVICE_ID_JOYPAD_R2,     // PAD_R2
	RETRO_DEVICE_ID_JOYPAD_L,      // PAD_L1
	RETRO_DEVICE_ID_JOYPAD_R,      // PAD_R1
	RETRO_DEVICE_ID_JOYPAD_X,      // PAD_TRIANGLE
	RETRO_DEVICE_ID_JOYPAD_A,      // PAD_CIRCLE
	RETRO_DEVICE_ID_JOYPAD_B,      // PAD_CROSS
	RETRO_DEVICE_ID_JOYPAD_Y,      // PAD_SQUARE
	RETRO_DEVICE_ID_JOYPAD_SELECT, // PAD_SELECT
	RETRO_DEVICE_ID_JOYPAD_L3,     // PAD_L3
	RETRO_DEVICE_ID_JOYPAD_R3,     // PAD_R3
	RETRO_DEVICE_ID_JOYPAD_START,  // PAD_START
	RETRO_DEVICE_ID_JOYPAD_UP,     // PAD_UP
	RETRO_DEVICE_ID_JOYPAD_RIGHT,  // PAD_RIGHT
	RETRO_DEVICE_ID_JOYPAD_DOWN,   // PAD_DOWN
	RETRO_DEVICE_ID_JOYPAD_LEFT,   // PAD_LEFT
};

namespace Input
{
	void Init()
	{
		environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble);
		static const struct retro_controller_description ds2_desc[] = {
			{"DualShock 2", RETRO_DEVICE_JOYPAD},
		};

		static const struct retro_controller_info ports[] = {
			{ds2_desc, sizeof(ds2_desc) / sizeof(*ds2_desc)},
			{ds2_desc, sizeof(ds2_desc) / sizeof(*ds2_desc)},
			{},
		};

		environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
		//	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
		button_mask[0] = 0xFFFFFFFF;
		button_mask[1] = 0xFFFFFFFF;
	}

	void Shutdown()
	{
		button_mask[0] = 0xFFFFFFFF;
		button_mask[1] = 0xFFFFFFFF;
	}

	void Update()
	{
		poll_cb();

		for (unsigned port = 0; port < 2; port++)
		{
			u32 mask                 = input_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
			u32 new_button_mask      = 0xFFFF0000;
			for (int i = 0; i < 16; i++)
				new_button_mask |= !(mask & (1 << keymap[i])) << i;
			button_mask[port]        = new_button_mask;
			pad_lx[port]             = input_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
			pad_ly[port]             = input_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
			pad_rx[port]             = input_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,  RETRO_DEVICE_ID_ANALOG_X);
			pad_ry[port]             = input_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,  RETRO_DEVICE_ID_ANALOG_Y);
			for (unsigned slot = 0; slot < 4; slot++)
				pads[port][slot].rumble(sioConvertPortAndSlotToPad(port, slot));
		}
	}

} // namespace Input

void retro_set_input_poll(retro_input_poll_t cb)
{
	poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
	input_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
	if (pad_type[port] != (int)device)
	{
		SettingsInterface* si = Host::Internal::GetBaseSettingsLayer();
		char section[8];
		snprintf(section, sizeof(section), "Pad%u", port + 1);
		pad_type[port] = device;

		switch (device)
		{
			case RETRO_DEVICE_JOYPAD:
				si->SetStringValue(section, "Type", "DualShock2");
				break;
			default:
				si->SetStringValue(section, "Type", "None");
				break;
		}

		PAD::LoadConfig(*si);
		environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
	}
}

#if 0
void Device::DoRumble(unsigned type, unsigned pad)
{
	if (pad >= GAMEPAD_NUMBER)
		return;

	if (type == 0)
		rumble.set_rumble_state(pad, RETRO_RUMBLE_WEAK, 0xFFFF);
	else
		rumble.set_rumble_state(pad, RETRO_RUMBLE_STRONG, 0xFFFF);
}
#endif

//////////////////////////////////////////////////////////////////////
// Pad implementation
//////////////////////////////////////////////////////////////////////

void Pad::reset()
{
	memset(this, 0, sizeof(PadFreezeData));

	mode           = MODE_DIGITAL;
	umask[0]       = 0xFF;
	umask[1]       = 0xFF;
	umask[2]       = 0x03;
	// Sets up vibrate variable.
	nextVibrate[0] = 0;
	nextVibrate[1] = 0;
	memset(vibrate, 0xFF, sizeof(vibrate));
	vibrate[0]     = 0x5A;
}

void Pad::rumble(unsigned port)
{
	if (nextVibrate[0] == currentVibrate[0] && nextVibrate[1] == currentVibrate[1])
		return;

	currentVibrate[0] = nextVibrate[0];
	currentVibrate[1] = nextVibrate[1];
#if 0
	InputManager::SetPadVibrationIntensity(port,
		std::min(static_cast<float>(currentVibrate[0]) * g_key_status.m_vibration_scale[port][0] * (1.0f / 255.0f), 1.0f),
		std::min(static_cast<float>(currentVibrate[1]) * g_key_status.m_vibration_scale[port][1] * (1.0f / 255.0f), 1.0f)
	);
#endif
}

void Pad::stop_vibrate_all()
{
#if 0
	for (int i=0; i<8; i++)
	{
		SetVibrate(i&1, i>>1, 0, 0);
		SetVibrate(i&1, i>>1, 1, 0);
	}
#endif
	// FIXME equivalent ?
	for (int port = 0; port < 2; port++)
	{
		for (int slot = 0; slot < 4; slot++)
		{
			pads[port][slot].nextVibrate[0] = 0;
			pads[port][slot].nextVibrate[1] = 0;
			memset(pads[port][slot].vibrate, 0xFF, sizeof(pads[port][slot].vibrate));
			pads[port][slot].vibrate[0] = 0x5A;
		}
	}
}

//////////////////////////////////////////////////////////////////////
// Pad implementation
//////////////////////////////////////////////////////////////////////

s32 PADinit(void)
{
	for (int port = 0; port < 2; port++)
		for (int slot = 0; slot < 4; slot++)
			pads[port][slot].reset();

	query.port           = 0;
	query.slot           = 0;
	query.lastByte       = 1;
	query.currentCommand = 0;
	query.numBytes       = 0;
	query.queryDone      = 1;
	memset(query.response, 0xF3, sizeof(query.response));

	for (int port = 0; port < 2; port++)
		slots[port] = 0;

	return 0;
}

void PADshutdown(void) { }
s32 PADopen(void)      { return 0; }
void PADclose(void)    { }

s32 PADsetSlot(u8 port, u8 slot)
{
	port--;
	slot--;
	if (port > 1 || slot > 3)
		return 0;
	// Even if no pad there, record the slot, as it is the active slot regardless.
	slots[port] = slot;

	return 1;
}

s32 PADfreeze(FreezeAction mode, freezeData* data)
{
	if (!data)
		return -1;

	if (mode == FreezeAction::Size)
		data->size = sizeof(PadFullFreezeData);
	else if (mode == FreezeAction::Load)
	{
		PadFullFreezeData* pdata = (PadFullFreezeData*)(data->data);

		Pad::stop_vibrate_all();

		if ((data->size != sizeof(PadFullFreezeData)))
			return 0;

		query = pdata->query;
		if (pdata->query.slot < 4)
			query = pdata->query;

		// Tales of the Abyss - pad fix
		// - restore data for both ports
		for (int port = 0; port < 2; port++)
		{
			for (int slot = 0; slot < 4; slot++)
			{
				u8 mode = pdata->padData[port][slot].mode;

				if (mode != MODE_DIGITAL && mode != MODE_ANALOG && mode != MODE_DS2_NATIVE)
					break;

				memcpy(&pads[port][slot], &pdata->padData[port][slot], sizeof(PadFreezeData));
			}

			if (pdata->slot[port] < 4)
				slots[port] = pdata->slot[port];
		}
	}
	else if (mode == FreezeAction::Save)
	{
		if (data->size != sizeof(PadFullFreezeData))
			return 0;

		PadFullFreezeData* pdata = (PadFullFreezeData*)(data->data);

		// Tales of the Abyss - pad fix
		// - PCSX2 only saves port0 (save #1), then port1 (save #2)

		memset(pdata, 0, data->size);
		pdata->query = query;

		for (int port = 0; port < 2; port++)
		{
			for (int slot = 0; slot < 4; slot++)
				pdata->padData[port][slot] = pads[port][slot];

			pdata->slot[port] = slots[port];
		}
	}
	else
		return -1;

	return 0;
}

u8 PADstartPoll(int _port, int _slot)
{
	if (_port >= 2)
	{
		query.port           = 0;
		query.slot           = 0;
		query.lastByte       = 1;
		query.currentCommand = 0;
		query.numBytes       = 0;
		query.queryDone      = 1;
		memset(query.response, 0xF3, sizeof(query.response));
		return 0;
	}

	query.port = _port;
	query.slot = _slot;

	const u32 ext_port = sioConvertPortAndSlotToPad(query.port, query.slot);

	if (g_key_status.m_type[ext_port] == ControllerType::NotConnected)
	{
		query.queryDone = 1;
		query.numBytes  = 0;
		query.lastByte  = 1;
		return 0;
	}
	query.queryDone         = 0;
	query.numBytes          = 2;
	query.lastByte          = 0;

	return 0xFF;
}

u8 PADpoll(u8 value)
{
	if (query.lastByte + 1 >= query.numBytes)
		return 0;
	if (query.lastByte && query.queryDone)
		return query.response[++query.lastByte];

	Pad* pad = &pads[query.port][query.slot];

	if (query.lastByte == 0)
	{
		query.lastByte++;
		query.currentCommand = value;

		switch (value)
		{
			case CMD_CONFIG_MODE:
				if (pad->config)
				{
					// In config mode.  Might not actually be leaving it.
					query.set_result(ConfigExit);
					return 0xF3;
				}
				// fallthrough on purpose (but I don't know why)
			case CMD_READ_DATA_AND_VIBRATE:
				{
					query.response[2] = 0x5A;
#if 0
					int i;
					Update(query.port, query.slot);
					ButtonSum *sum = &pad->sum;

					u8 b1 = 0xFF, b2 = 0xFF;
					for (i = 0; i<4; i++)
						b1 -= (sum->buttons[i]   > 0) << i;
					for (i = 0; i<8; i++)
						b2 -= (sum->buttons[i+4] > 0) << i;

					// FIXME
					if (config.padConfigs[query.port][query.slot].type == GuitarPad && !config.GH2) {
						sum->buttons[15] = 255;
						// Not sure about this.  Forces wammy to be from 0 to 0x7F.
						// if (sum->sticks[2].vert > 0) sum->sticks[2].vert = 0;
					}

					for (i = 4; i<8; i++)
						b1 -= (sum->buttons[i+8] > 0) << i;

					// FIXME
					//Left, Right and Down are always pressed on Pop'n Music controller.
					if (config.padConfigs[query.port][query.slot].type == PopnPad)
						b1=b1 & 0x1f;
#endif

					const u32 ext_port = sioConvertPortAndSlotToPad(query.port, query.slot);
					const u32 buttons  = button_mask[ext_port];
					if (!TEST_BIT(buttons, PAD_ANALOG) && !pad->modeLock)
					{
						switch (pad->mode)
						{
							case MODE_ANALOG:
							case MODE_DS2_NATIVE:
								pad->mode = MODE_DIGITAL;
								break;
							case MODE_DIGITAL:
							default:
								pad->mode = MODE_ANALOG;
								break;
						}
					}


					query.response[3]         = (buttons >> 8) & 0xFF;
					query.response[4]         = (buttons >> 0) & 0xFF;

					if (pad->mode != MODE_DIGITAL) // ANALOG || DS2 native
					{
						query.response[5] = static_cast<u8>(std::clamp(0x80 + (pad_rx[ext_port] >> 8) * pad_axis_scale[ext_port], 0.f, 255.f));
						query.response[6] = static_cast<u8>(std::clamp(0x80 + (pad_ry[ext_port] >> 8) * pad_axis_scale[ext_port], 0.f, 255.f));
						query.response[7] = static_cast<u8>(std::clamp(0x80 + (pad_lx[ext_port] >> 8) * pad_axis_scale[ext_port], 0.f, 255.f));
						query.response[8] = static_cast<u8>(std::clamp(0x80 + (pad_ly[ext_port] >> 8) * pad_axis_scale[ext_port], 0.f, 255.f));

						if (pad->mode != MODE_ANALOG) /* DS2 native */
						{
							query.numBytes             = 21;

							query.response[9]          = TEST_BIT(buttons, 13) ? 0 : 0xFF; /* Right */
							query.response[10]         = TEST_BIT(buttons, 15) ? 0 : 0xFF; /* Left  */
							query.response[11]         = TEST_BIT(buttons, 12) ? 0 : 0xFF; /* Up    */
							query.response[12]         = TEST_BIT(buttons, 14) ? 0 : 0xFF; /* Down  */
							query.response[13]         = TEST_BIT(buttons,  4) ? 0 : 0xFF; /* Triangle */
							query.response[14]         = TEST_BIT(buttons,  5) ? 0 : 0xFF; /* Circle   */
							query.response[15]         = TEST_BIT(buttons,  6) ? 0 : 0xFF; /* Cross    */
							query.response[16]         = TEST_BIT(buttons,  7) ? 0 : 0xFF; /* Square   */
							query.response[17]         = TEST_BIT(buttons,  2) ? 0 : 0xFF; /* L1       */
							query.response[18]         = TEST_BIT(buttons,  3) ? 0 : 0xFF; /* R1       */
							query.response[19]         = TEST_BIT(buttons,  0) ? 0 : 0xFF; /* L2       */
							query.response[20]         = TEST_BIT(buttons,  1) ? 0 : 0xFF; /* R2       */
						}
						else
							query.numBytes             = 9;
					}
					else
						query.numBytes                     = 5;

				}

				query.lastByte = 1;
				return pad->mode;

			case CMD_SET_VREF_PARAM:
				query.set_result(noclue);
				query.queryDone = 1;
				break;

			case CMD_QUERY_DS2_ANALOG_MODE:
				// Right?  Wrong?  No clue.
				if (pad->mode == MODE_DIGITAL)
				{
					queryMaskMode[1] = 0;
					queryMaskMode[2] = 0;
					queryMaskMode[3] = 0;
					queryMaskMode[6] = 0x00;
				}
				else
				{
					queryMaskMode[1] = pad->umask[0];
					queryMaskMode[2] = pad->umask[1];
					queryMaskMode[3] = pad->umask[2];
					// Not entirely sure about this.
					//queryMaskMode[3] = 0x01 | (pad->mode == MODE_DS2_NATIVE)*2;
					queryMaskMode[6] = 0x5A;
				}
				query.set_result(queryMaskMode);
				query.queryDone = 1;
				break;

			case CMD_SET_MODE_AND_LOCK:
				query.set_result(setMode);
				pad->nextVibrate[0] = 0;
				pad->nextVibrate[1] = 0;
				memset(pad->vibrate, 0xFF, sizeof(pad->vibrate));
				pad->vibrate[0] = 0x5A;
				break;

			case CMD_QUERY_MODEL_AND_MODE:
				if (IsDualshock2())
					query.set_result(queryModelDS2);
				else
					query.set_result(queryModelDS1);
				query.queryDone   = 1;
				// Not digital mode.
				query.response[5] = (pad->mode & 0xF) != 1;
				break;

			case CMD_QUERY_ACT:
				query.set_result(queryAct[0]);
				break;

			case CMD_QUERY_COMB:
				query.set_result(queryComb);
				query.queryDone = 1;
				break;

			case CMD_QUERY_MODE:
				query.set_result(queryMode);
				break;

			case CMD_VIBRATION_TOGGLE:
				memcpy(query.response + 2, pad->vibrate, 7);
				query.numBytes = 9;
#if 0
				query.set_result(pad->vibrate); // warning copy 7b not 8 (but it is really important?)
#endif
				pad->nextVibrate[0] = 0;
				pad->nextVibrate[1] = 0;
				memset(pad->vibrate, 0xFF, sizeof(pad->vibrate));
				pad->vibrate[0] = 0x5A;
				break;

			case CMD_SET_DS2_NATIVE_MODE:
				query.set_result(setNativeMode);
				if (!IsDualshock2())
					query.queryDone = 1;
				break;

			default:
				query.numBytes  = 0;
				query.queryDone = 1;
				break;
		}

		return 0xF3;
	}
	query.lastByte++;

	switch (query.currentCommand)
	{
		case CMD_READ_DATA_AND_VIBRATE:
			if (query.lastByte == pad->vibrateI[0])
				pad->nextVibrate[1] = 255 * (value & 1);
			else if (query.lastByte == pad->vibrateI[1])
				pad->nextVibrate[0] = value;

			break;

		case CMD_CONFIG_MODE:
			if (query.lastByte == 3)
			{
				query.queryDone = 1;
				pad->config = value;
			}
			break;

		case CMD_SET_MODE_AND_LOCK:
			if (query.lastByte == 3 && value < 2)
				pad->mode = value ? MODE_ANALOG : MODE_DIGITAL;
			else if (query.lastByte == 4)
			{
				if (value == 3)
					pad->modeLock = 3;
				else
					pad->modeLock = 0;

				query.queryDone = 1;
			}
			break;

		case CMD_QUERY_ACT:
			if (query.lastByte == 3)
			{
				if (value < 2)
					query.set_result(queryAct[value]);
				// bunch of 0's
				// else query.set_result(setMode);
				query.queryDone = 1;
			}
			break;

		case CMD_QUERY_MODE:
			if (query.lastByte == 3 && value < 2)
			{
				query.response[6] = 4 + value * 3;
				query.queryDone = 1;
			}
			// bunch of 0's
			//else data = setMode;
			break;

		case CMD_VIBRATION_TOGGLE:
			if (query.lastByte >= 3)
			{
				if (value == 0)
					pad->vibrateI[0] = (u8)query.lastByte;
				else if (value == 1)
					pad->vibrateI[1] = (u8)query.lastByte;
				pad->vibrate[query.lastByte - 2] = value;
			}
			break;

		case CMD_SET_DS2_NATIVE_MODE:
			if (query.lastByte > 2 && query.lastByte < 6)
				pad->umask[query.lastByte - 3] = value;
			pad->mode = MODE_DS2_NATIVE;
			break;

		default:
			return 0;
	}

	return query.response[query.lastByte];
}

bool PADcomplete(void) { return query.queryDone; }

void PAD::LoadConfig(const SettingsInterface& si)
{
	EmuConfig.MultitapPort0_Enabled = si.GetBoolValue("Pad", "MultitapPort1", false);
	EmuConfig.MultitapPort1_Enabled = si.GetBoolValue("Pad", "MultitapPort2", false);

	// This is where we would load controller types.
	for (u32 i = 0; i < NUM_CONTROLLER_PORTS; i++)
	{
		char section_c[32];
		snprintf(section_c, sizeof(section_c), "Pad%d", i + 1);
		const std::string type(si.GetStringValue(section_c, "Type", (i == 0) ? "DualShock2" : "None"));

		g_key_status.m_type[i]     = NotConnected;

		for (const ControllerInfo& info : s_controller_info)
		{
			if (type == info.name)
			{
				const float large_motor_scale      = si.GetFloatValue(section_c, "LargeMotorScale", DEFAULT_MOTOR_SCALE);
				const float small_motor_scale      = si.GetFloatValue(section_c, "SmallMotorScale", DEFAULT_MOTOR_SCALE);

				if (info.vibration_caps != NoVibration)
				{
					g_key_status.m_vibration_scale[i][0] = large_motor_scale;
					g_key_status.m_vibration_scale[i][1] = small_motor_scale;
				}

				g_key_status.m_type[i]     = info.type;
			}
		}
	}
}
