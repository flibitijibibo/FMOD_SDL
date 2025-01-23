/* FMOD_SDL: SDL Audio Output Plugin for FMOD Studio
 *
 * Copyright (c) 2018-2025 Ethan Lee
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#include <SDL3/SDL.h>
#include "fmod.h"
#include "fmod_output.h"
#ifdef PRELOAD_MODE
#include "fmod_studio.h"
#endif

/* Public API */

#define FMOD_SDL_VERSION 250123

#ifdef __cplusplus
extern "C" {
#endif

F_EXPORT void FMOD_SDL_Register(FMOD_SYSTEM *system);

#ifdef __cplusplus
}
#endif

/* Driver Implementation */

typedef struct FMOD_SDL_Device
{
	SDL_AudioStream *device;
	void *stagingBuffer;
	size_t stagingLen;
	Uint8 frameSize;
} FMOD_SDL_Device;

static void FMOD_SDL_MixCallback(
	void *userdata,
	SDL_AudioStream *stream,
	int additional_amount,
	int total_amount
) {
	FMOD_OUTPUT_STATE *output_state = (FMOD_OUTPUT_STATE*) userdata;
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	if (output_state->readfrommixer(
		output_state,
		dev->stagingBuffer,
		dev->stagingLen / dev->frameSize
	) == FMOD_OK) {
		SDL_PutAudioStreamData(
			stream,
			dev->stagingBuffer,
			dev->stagingLen
		);
	}
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_GetNumDrivers(
	FMOD_OUTPUT_STATE *output_state,
	int *numdrivers
) {
	SDL_free(SDL_GetAudioPlaybackDevices(numdrivers));
	if (*numdrivers > 0)
	{
		*numdrivers += 1;
	}
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_GetDriverInfo(
	FMOD_OUTPUT_STATE *output_state,
	int id,
	char *name,
	int namelen,
	FMOD_GUID *guid,
	int *systemrate,
	FMOD_SPEAKERMODE *speakermode,
	int *speakermodechannels
) {
	const char *envvar;
	SDL_AudioSpec spec;
	int devcount;
	SDL_AudioDeviceID *devs;

	devs = SDL_GetAudioPlaybackDevices(&devcount);

	SDL_strlcpy(
		name,
		(id == 0) ? "SDL Default" : SDL_GetAudioDeviceName(devs[id - 1]),
		namelen
	);

	SDL_memset(guid, '\0', sizeof(FMOD_GUID));


	/* Environment variables take precedence over all possible values */
	envvar = SDL_getenv("SDL_AUDIO_FREQUENCY");
	if (envvar != NULL)
	{
		*systemrate = SDL_atoi(envvar);
	}
	else
	{
		*systemrate = 0;
	}
	envvar = SDL_getenv("SDL_AUDIO_CHANNELS");
	if (envvar != NULL)
	{
		*speakermodechannels = SDL_atoi(envvar);
	}
	else
	{
		*speakermodechannels = 0;
	}

	if (id == 0)
	{
		if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL))
		{
			SDL_zero(spec);
		}
	}
	else
	{
		SDL_GetAudioDeviceFormat(devs[id - 1], &spec, NULL);
	}
	SDL_free(devs);
	if ((spec.freq > 0) && (*systemrate <= 0))
	{
		*systemrate = spec.freq;
	}
	if ((spec.channels > 0) && (*speakermodechannels <= 0))
	{
		*speakermodechannels = spec.channels;
	}

	/* If we make it all the way here with no format, hardcode a sane one */
	if (*systemrate <= 0)
	{
		*systemrate = 48000;
	}
	if (*speakermodechannels <= 0)
	{
		*speakermodechannels = 2;
	}

	switch (*speakermodechannels)
	{
	#define SPEAKERS(count, type) \
		case count: *speakermode = FMOD_SPEAKERMODE_##type; break;
	SPEAKERS(1, MONO)
	SPEAKERS(2, STEREO)
	SPEAKERS(4, QUAD)
	SPEAKERS(5, SURROUND)
	SPEAKERS(6, 5POINT1)
	SPEAKERS(8, 7POINT1)
	SPEAKERS(12, 7POINT1POINT4)
	#undef SPEAKERS
	default:
		SDL_Log("Unrecognized speaker layout!");
		return FMOD_ERR_OUTPUT_FORMAT;
	}

	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Init(
	FMOD_OUTPUT_STATE *output_state,
	int selecteddriver,
	FMOD_INITFLAGS flags,
	int *outputrate,
	FMOD_SPEAKERMODE *speakermode,
	int *speakermodechannels,
	FMOD_SOUND_FORMAT *outputformat,
	int dspbufferlength,
#if FMOD_VERSION >= 0x00020203
	int *dspnumbuffers,
	int *dspnumadditionalbuffers,
#else
	int dspnumbuffers,
#endif
	void *extradriverdata
) {
	FMOD_SDL_Device *device;
	SDL_AudioDeviceID devID;
	SDL_AudioSpec spec;
	const char *envvar;

	/* Before we start: Replicate FMOD's PulseAudio stream name support:
	 * https://www.fmod.org/questions/question/how-to-set-pulseaudio-program-name/
	 */
	if (extradriverdata != NULL)
	{
		const char *streamname = (char*) extradriverdata;
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_ICON_NAME, streamname);
	}

	if (selecteddriver == 0)
	{
		devID = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	}
	else
	{
		int devcount;
		SDL_AudioDeviceID *devs = SDL_GetAudioPlaybackDevices(&devcount);

		/* Bounds checking is done before this function is called */
		devID = devs[selecteddriver - 1];

		SDL_free(devs);
	}
	if (!SDL_GetAudioDeviceFormat(devID, &spec, NULL))
	{
		SDL_zero(spec);

		envvar = SDL_getenv("SDL_AUDIO_FREQUENCY");
		if (envvar != NULL)
		{
			spec.freq = SDL_atoi(envvar);
		}
		envvar = SDL_getenv("SDL_AUDIO_CHANNELS");
		if (envvar != NULL)
		{
			spec.channels = SDL_atoi(envvar);
		}
	}

	/* What do we want? */
	if (*outputrate > 0)
	{
		spec.freq = *outputrate;
	}
	if (*speakermodechannels > 0)
	{
		spec.channels = *speakermodechannels;
	}
	switch (*outputformat)
	{
	#define FORMAT(fmod, sdl) \
		case FMOD_SOUND_FORMAT_##fmod: spec.format = SDL_AUDIO_##sdl; break;
	FORMAT(PCM8, S8)
	FORMAT(PCM16, S16)
	FORMAT(PCM32, S32)
	FORMAT(PCMFLOAT, F32)
	#undef FORMAT
	default:
		SDL_Log("Unsupported FMOD PCM format!");
		return FMOD_ERR_OUTPUT_FORMAT;
	}

	/* Create the device, finally. */
	device = (FMOD_SDL_Device*) SDL_malloc(
		sizeof(FMOD_SDL_Device)
	);
	device->device = SDL_OpenAudioDeviceStream(
		devID,
		&spec,
		FMOD_SDL_MixCallback,
		output_state
	);
	if (device->device == NULL)
	{
		SDL_free(device);
		SDL_Log("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
		return FMOD_ERR_OUTPUT_INIT;
	}

	/* What did we get? */
	*outputrate = spec.freq;
	*speakermodechannels = spec.channels;
	switch (spec.channels)
	{
	#define SPEAKERS(count, type) \
		case count: *speakermode = FMOD_SPEAKERMODE_##type; break;
	SPEAKERS(1, MONO)
	SPEAKERS(2, STEREO)
	SPEAKERS(4, QUAD)
	SPEAKERS(5, SURROUND)
	SPEAKERS(6, 5POINT1)
	SPEAKERS(8, 7POINT1)
	SPEAKERS(12, 7POINT1POINT4)
	#undef SPEAKERS
	default:
		SDL_DestroyAudioStream(device->device);
		SDL_free(device);
		SDL_Log("Unrecognized speaker layout!");
		return FMOD_ERR_OUTPUT_INIT;
	}
	switch (spec.format)
	{
	#define FORMAT(sdl, fmod, size) \
		case SDL_AUDIO_##sdl: \
			*outputformat = FMOD_SOUND_FORMAT_##fmod; \
			device->frameSize = size; \
			break;
	FORMAT(S8, PCM8, 1)
	FORMAT(S16, PCM16, 2)
	FORMAT(S32, PCM32, 4)
	FORMAT(F32, PCMFLOAT, 4)
	#undef FORMAT
	default:
		SDL_DestroyAudioStream(device->device);
		SDL_free(device);
		SDL_Log("Unexpected SDL audio format!");
		return FMOD_ERR_OUTPUT_INIT;
	}
	device->frameSize *= spec.channels;

	device->stagingLen = dspbufferlength * device->frameSize;
	device->stagingBuffer = SDL_malloc(device->stagingLen);
	if (device->stagingBuffer == NULL)
	{
		SDL_DestroyAudioStream(device->device);
		SDL_free(device);
		return FMOD_ERR_OUTPUT_INIT;
	}

	/* We're ready to go! */
	output_state->plugindata = device;
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Start(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_ResumeAudioStreamDevice(dev->device);
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Stop(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_PauseAudioStreamDevice(dev->device);
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Close(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_DestroyAudioStream(dev->device);
	SDL_free(dev->stagingBuffer);
	SDL_free(dev);
	return FMOD_OK;
}

static FMOD_OUTPUT_DESCRIPTION FMOD_SDL_Driver =
{
	FMOD_OUTPUT_PLUGIN_VERSION,
	"FMOD_SDL",
	FMOD_SDL_VERSION,
#if FMOD_VERSION >= 0x00020000
	FMOD_OUTPUT_METHOD_MIX_DIRECT, /* We have our own thread! */
#else
	0,
#endif
	FMOD_SDL_GetNumDrivers,
	FMOD_SDL_GetDriverInfo,
	FMOD_SDL_Init,
	FMOD_SDL_Start,
	FMOD_SDL_Stop,
	FMOD_SDL_Close,
	NULL, /* Does anyone really want the native handle? */
	NULL, /* We have our own thread! */
	NULL, /* We have our own thread! */
	NULL, /* We have our own thread! */
	NULL, /* We have our own thread! */
	NULL, /* 3D object hardware...? */
	NULL, /* 3D object hardware...? */
	NULL, /* Auxiliary ports...? */
	NULL /* Auxiliary ports...? */
#if FMOD_VERSION >= 0x00020000
	, NULL /* FIXME: AUDIODEVICE events? */
#endif
};

/* Public API Implementation */

#ifndef PRELOAD_MODE
F_EXPORT void FMOD_SDL_Register(FMOD_SYSTEM *system)
{
	unsigned int handle;
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		SDL_Log("SDL_INIT_AUDIO failed: %s", SDL_GetError());
		return;
	}
	FMOD_System_RegisterOutput(system, &FMOD_SDL_Driver, &handle);
	FMOD_System_SetOutputByPlugin(system, handle);
}
#else
typedef FMOD_RESULT (*studioSystemCreateFunc)(
	FMOD_STUDIO_SYSTEM **system,
	unsigned int headerVersion
);
typedef FMOD_RESULT (*studioSystemGetCoreFunc)(
	FMOD_STUDIO_SYSTEM *system,
	FMOD_SYSTEM **coreSystem
);
typedef FMOD_RESULT (*systemRegisterOutputFunc)(
	FMOD_SYSTEM *system,
	FMOD_OUTPUT_DESCRIPTION *description,
	unsigned int *handle
);
typedef FMOD_RESULT (*systemSetOutputByPluginFunc)(
	FMOD_SYSTEM *system,
	unsigned int handle
);
FMOD_RESULT F_API FMOD_Studio_System_Create(
	FMOD_STUDIO_SYSTEM **system,
	unsigned int headerVersion
) {
	void* fmodlib;
	char fmodname[32];
	FMOD_RESULT result;
	unsigned int handle;
	FMOD_SYSTEM *core = NULL;
	studioSystemCreateFunc studioSystemCreate;
	studioSystemGetCoreFunc studioSystemGetCore;
	systemRegisterOutputFunc systemRegisterOutput;
	systemSetOutputByPluginFunc systemSetOutputByPlugin;

	/* Can't mix up versions, ABI breakages urrywhur */
	SDL_Log(
		"headerVersion: %X FMOD_VERSION: %X",
		headerVersion,
		FMOD_VERSION
	);
	SDL_assert(headerVersion == FMOD_VERSION);

	#define LOAD_FUNC(var, func) \
		var = (var##Func) SDL_LoadFunction(fmodlib, func);

	/* FMOD Studio entry points */
	SDL_snprintf(
		fmodname,
		sizeof(fmodname),
#if FMOD_VERSION >= 0x00020000
		"libfmodstudio.so.11" /* FIXME: FMOD screwed up their sonames! */
#else
		"libfmodstudio.so.%X", (headerVersion >> 8) & 0xFF
#endif
	);
	fmodlib = SDL_LoadObject(fmodname);
	LOAD_FUNC(studioSystemCreate, "FMOD_Studio_System_Create")
#if FMOD_VERSION >= 0x00020000
	LOAD_FUNC(studioSystemGetCore, "FMOD_Studio_System_GetCoreSystem")
#else
	/* Technically not the right name anymore, but whatever... */
	LOAD_FUNC(studioSystemGetCore, "FMOD_Studio_System_GetLowLevelSystem")
#endif

	/* Overloaded function */
	result = studioSystemCreate(system, headerVersion);
	if (result != FMOD_OK)
	{
		return result;
	}
	result = studioSystemGetCore(*system, &core);
	if (result != FMOD_OK)
	{
		return result;
	}
	/* mono needs this to leak :| SDL_UnloadObject(fmodlib); */

	/* FMOD entry points */
	SDL_snprintf(
		fmodname,
		sizeof(fmodname),
		"libfmod.so.%X",
		(headerVersion >> 8) & 0xFF
	);
	fmodlib = SDL_LoadObject(fmodname);
	LOAD_FUNC(systemRegisterOutput, "FMOD_System_RegisterOutput")
	LOAD_FUNC(systemSetOutputByPlugin, "FMOD_System_SetOutputByPlugin")

	/* FMOD_SDL_Register */
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		SDL_Log("SDL_INIT_AUDIO failed: %s", SDL_GetError());
		return FMOD_OK;
	}
	systemRegisterOutput(core, &FMOD_SDL_Driver, &handle);
	systemSetOutputByPlugin(core, handle);
	/* mono needs this to leak :| SDL_UnloadObject(fmodlib); */

	#undef LOAD_FUNC

	/* We out. */
	SDL_Log("FMOD_SDL is registered!");
	return FMOD_OK;
}
#endif
