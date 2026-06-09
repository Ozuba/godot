/**************************************************************************/
/*  audio_driver_switch.cpp                                               */
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

#include "audio_driver_switch.h"

#include "core/config/project_settings.h"
#include "core/os/os.h"

#include <malloc.h>
#include <cstring>

static const AudioRendererConfig audren_config = {
	.output_rate = AudioRendererOutputRate_48kHz,
	.num_voices = 24,
	.num_effects = 0,
	.num_sinks = 1,
	.num_mix_objs = 1,
	.num_mix_buffers = 2,
};

Error AudioDriverSwitch::init_device() {
	int latency = GLOBAL_GET("audio/driver/output_latency");
	mix_rate = _get_configured_mix_rate();
	channels = 2;
	speaker_mode = SPEAKER_MODE_STEREO;
	buffer_frames = Math::closest_power_of_2((uint32_t)(latency * mix_rate / 1000));

	samples_in.resize(buffer_frames * channels);
	samples_out.resize(buffer_frames * channels);

	Result res = audrenInitialize(&audren_config);
	if (R_FAILED(res)) {
		ERR_PRINT(vformat("audrenInitialize failed: 0x%x", (uint32_t)res));
		return ERR_CANT_OPEN;
	}

	res = audrvCreate(&audren_driver, &audren_config, 2);
	if (R_FAILED(res)) {
		ERR_PRINT(vformat("audrvCreate failed: 0x%x", (uint32_t)res));
		audrenExit();
		return ERR_CANT_OPEN;
	}

	audren_buffer_size = sizeof(int16_t) * buffer_frames * channels;
	audren_pool_size = ((audren_buffer_size * 2) + 0xFFF) & ~0xFFF;
	audren_pool_ptr = memalign(0x1000, audren_pool_size);
	memset(audren_pool_ptr, 0, audren_pool_size);

	for (int i = 0; i < 2; i++) {
		audren_buffers[i] = {};
		audren_buffers[i].data_raw = audren_pool_ptr;
		audren_buffers[i].size = audren_buffer_size * 2;
		audren_buffers[i].start_sample_offset = i * buffer_frames;
		audren_buffers[i].end_sample_offset = audren_buffers[i].start_sample_offset + buffer_frames;
	}

	int mpid = audrvMemPoolAdd(&audren_driver, audren_pool_ptr, audren_pool_size);
	audrvMemPoolAttach(&audren_driver, mpid);

	static const u8 sink_channels[] = { 0, 1 };
	audrvDeviceSinkAdd(&audren_driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

	audrvUpdate(&audren_driver);
	audrenStartAudioRenderer();

	audrvVoiceInit(&audren_driver, 0, channels, PcmFormat_Int16, mix_rate);
	audrvVoiceSetDestinationMix(&audren_driver, 0, AUDREN_FINAL_MIX_ID);
	audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 0, 0);
	audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 0, 1);
	audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 1, 0);
	audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 1, 1);

	return OK;
}

Error AudioDriverSwitch::init() {
	active.clear();
	exit_thread.clear();

	Error err = init_device();
	if (err == OK) {
		thread.start(AudioDriverSwitch::thread_func, this);
	}

	return err;
}

void AudioDriverSwitch::thread_func(void *p_udata) {
	AudioDriverSwitch *ad = static_cast<AudioDriverSwitch *>(p_udata);

	while (!ad->exit_thread.is_set()) {
		ad->lock();
		ad->start_counting_ticks();

		if (!ad->active.is_set()) {
			for (unsigned int i = 0; i < ad->buffer_frames * ad->channels; i++) {
				ad->samples_out.write[i] = 0;
			}
		} else {
			ad->audio_server_process(ad->buffer_frames, ad->samples_in.ptrw());
			for (unsigned int i = 0; i < ad->buffer_frames * ad->channels; i++) {
				ad->samples_out.write[i] = ad->samples_in[i] >> 16;
			}
		}

		int free_buffer = -1;
		for (int i = 0; i < 2; i++) {
			if (ad->audren_buffers[i].state == AudioDriverWaveBufState_Free || ad->audren_buffers[i].state == AudioDriverWaveBufState_Done) {
				free_buffer = i;
				break;
			}
		}

		if (free_buffer >= 0) {
			uint8_t *ptr = (uint8_t *)ad->audren_pool_ptr + (free_buffer * ad->audren_buffer_size);
			memcpy(ptr, ad->samples_out.ptr(), ad->audren_buffer_size);
			armDCacheFlush(ptr, ad->audren_buffer_size);
			audrvVoiceAddWaveBuf(&ad->audren_driver, 0, &ad->audren_buffers[free_buffer]);
			if (!audrvVoiceIsPlaying(&ad->audren_driver, 0)) {
				audrvVoiceStart(&ad->audren_driver, 0);
			}
			audrvUpdate(&ad->audren_driver);
			audrenWaitFrame();
			while (!ad->exit_thread.is_set() && ad->audren_buffers[free_buffer].state != AudioDriverWaveBufState_Playing) {
				audrvUpdate(&ad->audren_driver);
			}

			ad->stop_counting_ticks();
			ad->unlock();
		} else {
			ad->stop_counting_ticks();
			ad->unlock();
			OS::get_singleton()->delay_usec(1000);
		}
	}
}

void AudioDriverSwitch::start() {
	active.set();
}

int AudioDriverSwitch::get_mix_rate() const {
	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverSwitch::get_speaker_mode() const {
	return speaker_mode;
}

void AudioDriverSwitch::lock() {
	mutex.lock();
}

void AudioDriverSwitch::unlock() {
	mutex.unlock();
}

void AudioDriverSwitch::finish() {
	exit_thread.set();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}

	audrvClose(&audren_driver);
	audrenExit();

	if (audren_pool_ptr) {
		free(audren_pool_ptr);
		audren_pool_ptr = nullptr;
	}
}

AudioDriverSwitch::AudioDriverSwitch() {
}
