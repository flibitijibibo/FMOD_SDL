/* FMOD_SDL: SDL Audio Output Plugin for FMOD Studio
 *
 * Copyright (c) 2018-2019 Ethan Lee
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

#include <SDL.h>
#include "fmod.h"
#include "fmod_output.h"
#ifdef PRELOAD_MODE
#include "fmod_studio.h"
#endif

/* Public API */

#define FMOD_SDL_VERSION 190916

F_EXPORT void FMOD_SDL_Register(FMOD_SYSTEM *system);

/* Driver Implementation */

typedef struct FMOD_SDL_Device
{
	SDL_AudioDeviceID device;
	Uint8 frameSize;
} FMOD_SDL_Device;

static void FMOD_SDL_MixCallback(void* userdata, Uint8 *stream,	int len)
{
	FMOD_OUTPUT_STATE *output_state = (FMOD_OUTPUT_STATE*) userdata;
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	if (output_state->readfrommixer(
		output_state,
		stream,
		len / dev->frameSize
	) != FMOD_OK) {
		SDL_memset(stream, '\0', len);
	}
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_GetNumDrivers(
	FMOD_OUTPUT_STATE *output_state,
	int *numdrivers
) {
	*numdrivers = SDL_GetNumAudioDevices(0);
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

	SDL_strlcpy(
		name,
		(id == 0) ? "SDL Default" : SDL_GetAudioDeviceName(id - 1, 0),
		namelen
	);

	SDL_memset(guid, '\0', sizeof(FMOD_GUID));

	/* TODO: SDL_GetAudioDeviceSpec */
	envvar = SDL_getenv("SDL_AUDIO_FREQUENCY");
	if (!envvar || ((*systemrate = SDL_atoi(envvar)) == 0))
	{
		*systemrate = 48000;
	}
	envvar = SDL_getenv("SDL_AUDIO_CHANNELS");
	if (!envvar || ((*speakermodechannels = SDL_atoi(envvar)) == 0))
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
	int dspnumbuffers,
	void *extradriverdata
) {
	FMOD_SDL_Device *device;
	SDL_AudioSpec want, have;

	/* What do we want? */
	want.freq = *outputrate;
	want.channels = *speakermodechannels;
	switch (*outputformat)
	{
	#define FORMAT(fmod, sdl) \
		case FMOD_SOUND_FORMAT_##fmod: want.format = AUDIO_##sdl; break;
	FORMAT(PCM8, S8)
	FORMAT(PCM16, S16SYS)
	FORMAT(PCM32, S32SYS)
	FORMAT(PCMFLOAT, F32SYS)
	#undef FORMAT
	default:
		SDL_Log("Unsupported FMOD PCM format!");
		return FMOD_ERR_OUTPUT_FORMAT;
	}
	want.silence = 0;
	want.callback = FMOD_SDL_MixCallback;
	want.samples = dspbufferlength;
	want.userdata = output_state;

	/* Create the device, finally. */
	device = (FMOD_SDL_Device*) SDL_malloc(
		sizeof(FMOD_SDL_Device)
	);
	device->device = SDL_OpenAudioDevice(
		(selecteddriver == 0) ?
			NULL :
			SDL_GetAudioDeviceName(selecteddriver - 1, 0),
		0,
		&want,
		&have,
		(
			SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
			SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
			SDL_AUDIO_ALLOW_FORMAT_CHANGE
		)
	);
	if (device->device < 0)
	{
		SDL_free(device);
		SDL_Log("OpenAudioDevice failed: %s", SDL_GetError());
		return FMOD_ERR_OUTPUT_INIT;
	}

	/* What did we get? */
	*outputrate = have.freq;
	*speakermodechannels = have.channels;
	switch (have.channels)
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
		SDL_CloseAudioDevice(device->device);
		SDL_free(device);
		SDL_Log("Unrecognized speaker layout!");
		return FMOD_ERR_OUTPUT_INIT;
	}
	switch (have.format)
	{
	#define FORMAT(sdl, fmod, size) \
		case AUDIO_##sdl: \
			*outputformat = FMOD_SOUND_FORMAT_##fmod; \
			device->frameSize = size; \
			break;
	FORMAT(S8, PCM8, 1)
	FORMAT(S16SYS, PCM16, 2)
	FORMAT(S32SYS, PCM32, 4)
	FORMAT(F32SYS, PCMFLOAT, 4)
	#undef FORMAT
	default:
		SDL_CloseAudioDevice(device->device);
		SDL_free(device);
		SDL_Log("Unexpected SDL audio format!");
		return FMOD_ERR_OUTPUT_INIT;
	}
	device->frameSize *= have.channels;

	/* We're ready to go! */
	output_state->plugindata = device;
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Start(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_PauseAudioDevice(dev->device, 0);
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Stop(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_PauseAudioDevice(dev->device, 1);
	return FMOD_OK;
}

static FMOD_RESULT F_CALLBACK FMOD_SDL_Close(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_Device *dev = (FMOD_SDL_Device*)
		output_state->plugindata;
	SDL_CloseAudioDevice(dev->device);
	SDL_free(dev);
	return FMOD_OK;
}

static FMOD_OUTPUT_DESCRIPTION FMOD_SDL_Driver =
{
	FMOD_OUTPUT_PLUGIN_VERSION,
	"FMOD_SDL",
	FMOD_SDL_VERSION,
	0, /* We have our own thread! */
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
};

/* Public API Implementation */

#ifndef PRELOAD_MODE
F_EXPORT void FMOD_SDL_Register(FMOD_SYSTEM *system)
{
	unsigned int handle;
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
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
typedef FMOD_RESULT (*studioSystemGetLowLevelFunc)(
	FMOD_STUDIO_SYSTEM *system,
	FMOD_SYSTEM **lowLevelSystem
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
	FMOD_SYSTEM *lowLevel = NULL;
	studioSystemCreateFunc studioSystemCreate;
	studioSystemGetLowLevelFunc studioSystemGetLowLevel;
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
		"libfmodstudio.so.%X",
		(headerVersion >> 8) & 0xFF
	);
	fmodlib = SDL_LoadObject(fmodname);
	LOAD_FUNC(studioSystemCreate, "FMOD_Studio_System_Create")
	LOAD_FUNC(studioSystemGetLowLevel, "FMOD_Studio_System_GetLowLevelSystem")

	/* Overloaded function */
	result = studioSystemCreate(system, headerVersion);
	if (result != FMOD_OK)
	{
		return result;
	}
	result = studioSystemGetLowLevel(*system, &lowLevel);
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
	systemRegisterOutput(lowLevel, &FMOD_SDL_Driver, &handle);
	systemSetOutputByPlugin(lowLevel, handle);
	/* mono needs this to leak :| SDL_UnloadObject(fmodlib); */

	#undef LOAD_FUNC

	/* We out. */
	SDL_Log("FMOD_SDL is registered!");
	return FMOD_OK;
}
#endif
