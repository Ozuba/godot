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

#include "core/os/os.h"

#include <cmath>
#include <iterator>

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

static HidNpadIdType _npad_id(int p_index, u32 p_style) {
	if (p_index == 0 && p_style == HidNpadStyleTag_NpadHandheld) {
		return HidNpadIdType_Handheld;
	}
	return (HidNpadIdType)(HidNpadIdType_No1 + p_index);
}

struct ButtonMapping {
	u64 hid_button;
	JoyButton joy_button;
};

// Map to Godot's SDL-style layout by position: A = south, B = east, X = west, Y = north.
static const ButtonMapping standard_mapping[] = {
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

// Single left Joy-Con held horizontally (SL/SR up, top edge pointing left):
// the face rotates 90 degrees counter-clockwise, so the d-pad becomes the
// face buttons.
static const ButtonMapping joy_left_mapping[] = {
	{ HidNpadButton_Left, JoyButton::A }, // South.
	{ HidNpadButton_Down, JoyButton::B }, // East.
	{ HidNpadButton_Up, JoyButton::X }, // West.
	{ HidNpadButton_Right, JoyButton::Y }, // North.
	{ HidNpadButton_Minus, JoyButton::START },
	{ HidNpadButton_StickL, JoyButton::LEFT_STICK },
	{ HidNpadButton_LeftSL, JoyButton::LEFT_SHOULDER },
	{ HidNpadButton_LeftSR, JoyButton::RIGHT_SHOULDER },
};

// Single right Joy-Con held horizontally (SL/SR up, top edge pointing right):
// the face rotates 90 degrees clockwise.
static const ButtonMapping joy_right_mapping[] = {
	{ HidNpadButton_A, JoyButton::A }, // South.
	{ HidNpadButton_X, JoyButton::B }, // East.
	{ HidNpadButton_B, JoyButton::X }, // West.
	{ HidNpadButton_Y, JoyButton::Y }, // North.
	{ HidNpadButton_Plus, JoyButton::START },
	{ HidNpadButton_StickR, JoyButton::LEFT_STICK },
	{ HidNpadButton_RightSL, JoyButton::LEFT_SHOULDER },
	{ HidNpadButton_RightSR, JoyButton::RIGHT_SHOULDER },
};

u32 JoypadSwitch::_active_style(u32 p_style_set) {
	static const u32 styles[] = {
		HidNpadStyleTag_NpadFullKey,
		HidNpadStyleTag_NpadHandheld,
		HidNpadStyleTag_NpadJoyDual,
		HidNpadStyleTag_NpadJoyLeft,
		HidNpadStyleTag_NpadJoyRight,
	};
	for (u32 style : styles) {
		if (p_style_set & style) {
			return style;
		}
	}
	return 0;
}

const char *JoypadSwitch::_style_name(u32 p_style) {
	switch (p_style) {
		case HidNpadStyleTag_NpadFullKey:
			return "Pro Controller";
		case HidNpadStyleTag_NpadHandheld:
			return "Switch Handheld";
		case HidNpadStyleTag_NpadJoyDual:
			return "Joy-Con (Dual)";
		case HidNpadStyleTag_NpadJoyLeft:
			return "Joy-Con (L)";
		case HidNpadStyleTag_NpadJoyRight:
			return "Joy-Con (R)";
		default:
			return "Switch Controller";
	}
}

void JoypadSwitch::_refresh_style(int p_index) {
	Pad &pad = pads[p_index];
	u32 style = _active_style(padGetStyleSet(&pad.state));
	if (style == pad.style) {
		return;
	}
	pad.style = style;
	pad.vibration_handle_count = 0;
	pad.sixaxis_handle_count = 0;
	if (style == 0) {
		return;
	}

	HidNpadIdType id = _npad_id(p_index, style);

	// HD rumble: dual Joy-Con and handheld have two actuators.
	int vib_count = (style == HidNpadStyleTag_NpadJoyDual || style == HidNpadStyleTag_NpadHandheld || style == HidNpadStyleTag_NpadFullKey) ? 2 : 1;
	if (R_SUCCEEDED(hidInitializeVibrationDevices(pad.vibration_handles, vib_count, id, (HidNpadStyleTag)style))) {
		pad.vibration_handle_count = vib_count;
	}

	// Motion: dual Joy-Con exposes two sensors, use the first.
	int six_count = (style == HidNpadStyleTag_NpadJoyDual) ? 2 : 1;
	if (R_SUCCEEDED(hidGetSixAxisSensorHandles(pad.sixaxis_handles, six_count, id, (HidNpadStyleTag)style))) {
		pad.sixaxis_handle_count = six_count;
		for (int i = 0; i < six_count; i++) {
			hidStartSixAxisSensor(pad.sixaxis_handles[i]);
		}
	}
}

void JoypadSwitch::_process_buttons_and_axes(int p_index) {
	Pad &pad = pads[p_index];

	HidAnalogStickState l_stick = padGetStickPos(&pad.state, 0);
	HidAnalogStickState r_stick = padGetStickPos(&pad.state, 1);

	if (pad.style == HidNpadStyleTag_NpadJoyLeft) {
		// Rotate the stick 90 degrees counter-clockwise for horizontal hold.
		input->joy_axis(p_index, JoyAxis::LEFT_X, -(float)l_stick.y / (float)JOYSTICK_MAX);
		input->joy_axis(p_index, JoyAxis::LEFT_Y, -(float)l_stick.x / (float)JOYSTICK_MAX);
	} else if (pad.style == HidNpadStyleTag_NpadJoyRight) {
		// Rotate the stick 90 degrees clockwise for horizontal hold.
		input->joy_axis(p_index, JoyAxis::LEFT_X, (float)r_stick.y / (float)JOYSTICK_MAX);
		input->joy_axis(p_index, JoyAxis::LEFT_Y, (float)r_stick.x / (float)JOYSTICK_MAX);
	} else {
		input->joy_axis(p_index, JoyAxis::LEFT_X, (float)l_stick.x / (float)JOYSTICK_MAX);
		input->joy_axis(p_index, JoyAxis::LEFT_Y, -(float)l_stick.y / (float)JOYSTICK_MAX);
		input->joy_axis(p_index, JoyAxis::RIGHT_X, (float)r_stick.x / (float)JOYSTICK_MAX);
		input->joy_axis(p_index, JoyAxis::RIGHT_Y, -(float)r_stick.y / (float)JOYSTICK_MAX);
	}

	u64 buttons_up = padGetButtonsUp(&pad.state);
	u64 buttons_down = padGetButtonsDown(&pad.state);
	if (buttons_up == 0 && buttons_down == 0) {
		return;
	}

	const ButtonMapping *mapping = standard_mapping;
	size_t mapping_count = sizeof(standard_mapping) / sizeof(standard_mapping[0]);
	if (pad.style == HidNpadStyleTag_NpadJoyLeft) {
		mapping = joy_left_mapping;
		mapping_count = sizeof(joy_left_mapping) / sizeof(joy_left_mapping[0]);
	} else if (pad.style == HidNpadStyleTag_NpadJoyRight) {
		mapping = joy_right_mapping;
		mapping_count = sizeof(joy_right_mapping) / sizeof(joy_right_mapping[0]);
	}

	for (size_t i = 0; i < mapping_count; i++) {
		if (buttons_up & mapping[i].hid_button) {
			input->joy_button(p_index, mapping[i].joy_button, false);
		}
		if (buttons_down & mapping[i].hid_button) {
			input->joy_button(p_index, mapping[i].joy_button, true);
		}
	}

	if (pad.style != HidNpadStyleTag_NpadJoyLeft && pad.style != HidNpadStyleTag_NpadJoyRight) {
		// Digital triggers, exposed as axes for compatibility with the
		// standard gamepad mapping.
		if (buttons_down & HidNpadButton_ZL) {
			input->joy_axis(p_index, JoyAxis::TRIGGER_LEFT, 1.0f);
		} else if (buttons_up & HidNpadButton_ZL) {
			input->joy_axis(p_index, JoyAxis::TRIGGER_LEFT, 0.0f);
		}
		if (buttons_down & HidNpadButton_ZR) {
			input->joy_axis(p_index, JoyAxis::TRIGGER_RIGHT, 1.0f);
		} else if (buttons_up & HidNpadButton_ZR) {
			input->joy_axis(p_index, JoyAxis::TRIGGER_RIGHT, 0.0f);
		}
	}
}

void JoypadSwitch::vibrate(int p_index, float p_weak, float p_strong, float p_duration_sec) {
	ERR_FAIL_INDEX(p_index, JOYPADS_MAX);
	Pad &pad = pads[p_index];
	if (pad.vibration_handle_count == 0) {
		return;
	}

	HidVibrationValue value = {};
	value.amp_low = CLAMP(p_strong, 0.0f, 1.0f);
	value.freq_low = 160.0f;
	value.amp_high = CLAMP(p_weak, 0.0f, 1.0f);
	value.freq_high = 320.0f;

	HidVibrationValue values[2] = { value, value };
	hidSendVibrationValues(pad.vibration_handles, values, pad.vibration_handle_count);

	pad.vibrating = (value.amp_low > 0.0f || value.amp_high > 0.0f);
	if (pad.vibrating && p_duration_sec > 0.0f) {
		pad.vibration_end_usec = OS::get_singleton()->get_ticks_usec() + (uint64_t)(p_duration_sec * 1000000.0f);
	} else {
		pad.vibration_end_usec = 0; // Until explicitly stopped.
	}
}

void JoypadSwitch::_process_vibration(int p_index) {
	Pad &pad = pads[p_index];
	if (pad.vibration_handle_count == 0) {
		return;
	}

	uint64_t timestamp = input->get_joy_vibration_timestamp(p_index);
	if (timestamp > pad.vibration_timestamp) {
		pad.vibration_timestamp = timestamp;
		Vector2 strength = input->get_joy_vibration_strength(p_index);
		float duration = input->get_joy_vibration_duration(p_index);
		vibrate(p_index, strength.x, strength.y, duration);
	} else if (pad.vibrating && pad.vibration_end_usec != 0 && OS::get_singleton()->get_ticks_usec() >= pad.vibration_end_usec) {
		vibrate(p_index, 0.0f, 0.0f, 0.0f);
		pad.vibrating = false;
	}
}

void JoypadSwitch::_process_sixaxis(int p_index) {
	// Only the first player (or handheld) feeds the device motion API.
	Pad &pad = pads[p_index];
	if (p_index != 0 || pad.sixaxis_handle_count == 0) {
		return;
	}

	HidSixAxisSensorState state = {};
	if (hidGetSixAxisSensorStates(pad.sixaxis_handles[0], &state, 1) < 1) {
		return;
	}

	// libnx reports acceleration in G and angular velocity in revolutions/s;
	// Godot expects m/s^2 and rad/s. Axes match Godot's device convention
	// (X right, Y up, Z out of the screen) when held in handheld mode.
	constexpr float standard_gravity = 9.80665f;
	constexpr float tau = 6.28318530718f;
	input->set_accelerometer(Vector3(state.acceleration.x, state.acceleration.y, state.acceleration.z) * standard_gravity);
	input->set_gyroscope(Vector3(state.angular_velocity.x, state.angular_velocity.y, state.angular_velocity.z) * tau);
}

void JoypadSwitch::process() {
	for (int index = 0; index < JOYPADS_MAX; index++) {
		Pad &pad = pads[index];
		padUpdate(&pad.state);

		bool is_connected = padIsConnected(&pad.state);
		if (is_connected != pad.connected) {
			pad.connected = is_connected;
			pad.style = 0;
			if (is_connected) {
				_refresh_style(index);
				Dictionary info;
				info["mapping_handled"] = true; // Buttons are already mapped to the SDL-style layout.
				input->joy_connection_changed(index, true, _style_name(pad.style), "", info);
			} else {
				input->joy_connection_changed(index, false, "");
				continue;
			}
		}

		if (!is_connected) {
			continue;
		}

		_refresh_style(index);
		_process_buttons_and_axes(index);
		_process_vibration(index);
		_process_sixaxis(index);
	}
}

JoypadSwitch::JoypadSwitch(Input *p_input) {
	input = p_input;

	padConfigureInput(JOYPADS_MAX, HidNpadStyleSet_NpadFullCtrl);
	// Single Joy-Cons are held horizontally; the mappings above match that.
	hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);

	for (int i = 0; i < JOYPADS_MAX; i++) {
		padInitializeWithMask(&pads[i].state, pad_ids[i]);
		padUpdate(&pads[i].state);
		pads[i].connected = padIsConnected(&pads[i].state);
		if (pads[i].connected) {
			_refresh_style(i);
			Dictionary info;
			info["mapping_handled"] = true;
			input->joy_connection_changed(i, true, _style_name(pads[i].style), "", info);
		}
	}
}

JoypadSwitch::~JoypadSwitch() {
	for (int i = 0; i < JOYPADS_MAX; i++) {
		if (pads[i].vibration_handle_count > 0) {
			vibrate(i, 0.0f, 0.0f, 0.0f);
		}
		for (int j = 0; j < pads[i].sixaxis_handle_count; j++) {
			hidStopSixAxisSensor(pads[i].sixaxis_handles[j]);
		}
	}
}
