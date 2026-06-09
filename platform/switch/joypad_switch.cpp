/**************************************************************************/
/*  joypad_switch.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "joypad_switch.h"

static const u64 pad_ids[JOYPADS_MAX] = {
	(1ull << HidNpadIdType_No1) | (1ull << HidNpadIdType_Handheld),
	(1ull << HidNpadIdType_No2),
	(1ull << HidNpadIdType_No3),
	(1ull << HidNpadIdType_No4),
	(1ull << HidNpadIdType_No5),
	(1ull << HidNpadIdType_No6),
	(1ull << HidNpadIdType_No7),
	(1ull << HidNpadIdType_No8),
};

struct ButtonMapping {
	HidNpadButton hid_button;
	JoyButton joy_button;
};

// Map to Godot's SDL-style layout by position: A = south, B = east, X = west, Y = north.
static const ButtonMapping pad_mapping[] = {
	{ HidNpadButton_B, JoyButton::A },
	{ HidNpadButton_A, JoyButton::B },
	{ HidNpadButton_Y, JoyButton::X },
	{ HidNpadButton_X, JoyButton::Y },
	{ HidNpadButton_Minus, JoyButton::BACK },
	{ HidNpadButton_Plus, JoyButton::START },
	{ HidNpadButton_StickL, JoyButton::LEFT_STICK },
	{ HidNpadButton_StickR, JoyButton::RIGHT_STICK },
	{ HidNpadButton_L, JoyButton::LEFT_SHOULDER },
	{ HidNpadButton_R, JoyButton::RIGHT_SHOULDER },
	{ HidNpadButton_Up, JoyButton::DPAD_UP },
	{ HidNpadButton_Down, JoyButton::DPAD_DOWN },
	{ HidNpadButton_Left, JoyButton::DPAD_LEFT },
	{ HidNpadButton_Right, JoyButton::DPAD_RIGHT },
};

JoypadSwitch::JoypadSwitch(Input *p_input) {
	input = p_input;

	padConfigureInput(JOYPADS_MAX, HidNpadStyleSet_NpadStandard);

	for (int i = 0; i < JOYPADS_MAX; i++) {
		padInitializeWithMask(&pads[i], pad_ids[i]);
		padUpdate(&pads[i]);
		connected[i] = padIsConnected(&pads[i]);
		if (connected[i]) {
			input->joy_connection_changed(i, true, "Switch Controller " + itos(i + 1), "");
		}
	}
}

JoypadSwitch::~JoypadSwitch() {
}

void JoypadSwitch::process() {
	for (int index = 0; index < JOYPADS_MAX; index++) {
		padUpdate(&pads[index]);

		bool is_connected = padIsConnected(&pads[index]);
		if (is_connected != connected[index]) {
			connected[index] = is_connected;
			input->joy_connection_changed(index, is_connected, "Switch Controller " + itos(index + 1), "");
		}

		if (!is_connected) {
			continue;
		}

		HidAnalogStickState l_stick = padGetStickPos(&pads[index], 0);
		HidAnalogStickState r_stick = padGetStickPos(&pads[index], 1);

		input->joy_axis(index, JoyAxis::LEFT_X, (float)l_stick.x / (float)JOYSTICK_MAX);
		input->joy_axis(index, JoyAxis::LEFT_Y, -(float)l_stick.y / (float)JOYSTICK_MAX);
		input->joy_axis(index, JoyAxis::RIGHT_X, (float)r_stick.x / (float)JOYSTICK_MAX);
		input->joy_axis(index, JoyAxis::RIGHT_Y, -(float)r_stick.y / (float)JOYSTICK_MAX);

		u64 buttons_up = padGetButtonsUp(&pads[index]);
		u64 buttons_down = padGetButtonsDown(&pads[index]);

		if (buttons_up != 0 || buttons_down != 0) {
			for (const ButtonMapping &mapping : pad_mapping) {
				if (buttons_up & mapping.hid_button) {
					input->joy_button(index, mapping.joy_button, false);
				}
				if (buttons_down & mapping.hid_button) {
					input->joy_button(index, mapping.joy_button, true);
				}
			}

			// Digital triggers, exposed as axes for compatibility with the
			// standard gamepad mapping.
			if (buttons_down & HidNpadButton_ZL) {
				input->joy_axis(index, JoyAxis::TRIGGER_LEFT, 1.0f);
			} else if (buttons_up & HidNpadButton_ZL) {
				input->joy_axis(index, JoyAxis::TRIGGER_LEFT, 0.0f);
			}
			if (buttons_down & HidNpadButton_ZR) {
				input->joy_axis(index, JoyAxis::TRIGGER_RIGHT, 1.0f);
			} else if (buttons_up & HidNpadButton_ZR) {
				input->joy_axis(index, JoyAxis::TRIGGER_RIGHT, 0.0f);
			}
		}
	}
}
