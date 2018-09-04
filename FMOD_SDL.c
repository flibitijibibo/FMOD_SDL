/* FMOD_SDL: SDL Audio Output Plugin for FMOD Studio
 *
 * Copyright (c) 2018 Ethan Lee
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

#define FMOD_SDL_VERSION 0

F_EXPORT void FMOD_SDL_Register(FMOD_SYSTEM *system);

/* Driver Implementation */

typedef struct FMOD_SDL_INTERNAL_Device
{
	SDL_AudioDeviceID device;
	Uint8 frameSize;
} FMOD_SDL_INTERNAL_Device;

static void FMOD_SDL_INTERNAL_MixCallback(
	void* userdata,
	Uint8 *stream,
	int len
) {
	FMOD_OUTPUT_STATE *output_state = (FMOD_OUTPUT_STATE*) userdata;
	FMOD_SDL_INTERNAL_Device *dev = (FMOD_SDL_INTERNAL_Device*)
		output_state->plugindata;
	if (output_state->readfrommixer(
		output_state,
		stream,
		len / dev->frameSize
	) != FMOD_OK) {
		SDL_memset(stream, '\0', len);
	}
}

FMOD_RESULT F_CALLBACK FMOD_SDL_INTERNAL_GetNumDrivers(
	FMOD_OUTPUT_STATE *output_state,
	int *numdrivers
) {
	*numdrivers = SDL_GetNumAudioDevices(0) + 1;
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_SDL_INTERNAL_GetDriverInfo(
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
	if (*speakermodechannels == 1)
	{
		*speakermode = FMOD_SPEAKERMODE_MONO;
	}
	else if (*speakermodechannels == 2)
	{
		*speakermode = FMOD_SPEAKERMODE_STEREO;
	}
	else if (*speakermodechannels == 4)
	{
		*speakermode = FMOD_SPEAKERMODE_QUAD;
	}
	else if (*speakermodechannels == 6)
	{
		*speakermode = FMOD_SPEAKERMODE_5POINT1;
	}
	else if (*speakermodechannels == 8)
	{
		*speakermode = FMOD_SPEAKERMODE_7POINT1;
	}
	else
	{
		SDL_assert(0 && "Unrecognized speaker layout!");
	}

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_SDL_INTERNAL_Init(
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
	FMOD_SDL_INTERNAL_Device *device;
	SDL_AudioSpec want, have;

	/* What do we want? */
	want.freq = *outputrate;
	want.channels = *speakermodechannels;
	if (*outputformat == FMOD_SOUND_FORMAT_PCMFLOAT)
	{
		want.format = AUDIO_F32;
	}
	else if (*outputformat == FMOD_SOUND_FORMAT_PCM16)
	{
		want.format = AUDIO_S16SYS;
	}
	else
	{
		SDL_assert(0 && "Unsupported FMOD PCM format!");
	}
	want.silence = 0;
	want.callback = FMOD_SDL_INTERNAL_MixCallback;
	want.samples = dspbufferlength;
	want.userdata = output_state;

	/* Create the device, finally. */
	device = (FMOD_SDL_INTERNAL_Device*) SDL_malloc(
		sizeof(FMOD_SDL_INTERNAL_Device)
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
		return FMOD_ERR_OUTPUT_INIT;
	}

	/* What did we get? */
	*outputrate = have.freq;
	*speakermodechannels = have.channels;
	if (have.channels == 1)
	{
		*speakermode = FMOD_SPEAKERMODE_MONO;
	}
	else if (have.channels == 2)
	{
		*speakermode = FMOD_SPEAKERMODE_STEREO;
	}
	else if (have.channels == 4)
	{
		*speakermode = FMOD_SPEAKERMODE_QUAD;
	}
	else if (have.channels == 6)
	{
		*speakermode = FMOD_SPEAKERMODE_5POINT1;
	}
	else if (have.channels == 8)
	{
		*speakermode = FMOD_SPEAKERMODE_7POINT1;
	}
	else
	{
		SDL_assert(0 && "Unrecognized speaker layout!");
	}
	if (have.format == AUDIO_F32)
	{
		*outputformat = FMOD_SOUND_FORMAT_PCMFLOAT;
		device->frameSize = 4;
	}
	else if (have.format == AUDIO_S16SYS)
	{
		*outputformat = FMOD_SOUND_FORMAT_PCM16;
		device->frameSize = 2;
	}
	else
	{
		SDL_assert(0 && "Unexpected SDL audio format!");
	}
	device->frameSize *= have.channels;

	/* We're running now! */
	output_state->plugindata = device;
	SDL_PauseAudioDevice(device->device, 0);
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_SDL_INTERNAL_Close(FMOD_OUTPUT_STATE *output_state)
{
	FMOD_SDL_INTERNAL_Device *dev = (FMOD_SDL_INTERNAL_Device*)
		output_state->plugindata;
	SDL_CloseAudioDevice(dev->device);
	SDL_free(dev);
	return FMOD_OK;
}

static FMOD_OUTPUT_DESCRIPTION FMOD_SDL_INTERNAL_Driver =
{
	FMOD_OUTPUT_PLUGIN_VERSION,
	"FMOD_SDL",
	FMOD_SDL_VERSION,
	0, /* We have our own thread! */
	FMOD_SDL_INTERNAL_GetNumDrivers,
	FMOD_SDL_INTERNAL_GetDriverInfo,
	FMOD_SDL_INTERNAL_Init,
	NULL, /* Nothing to do before mix updates... */
	NULL, /* Nothing to do after mix updates... */
	FMOD_SDL_INTERNAL_Close,
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
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	FMOD_System_RegisterOutput(system, &FMOD_SDL_INTERNAL_Driver, &handle);
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
	unsigned int handle;
	FMOD_SYSTEM *lowLevel = NULL;
	studioSystemCreateFunc studioSystemCreate;
	studioSystemGetLowLevelFunc studioSystemGetLowLevel;
	systemRegisterOutputFunc systemRegisterOutput;
	systemSetOutputByPluginFunc systemSetOutputByPlugin;

	/* Can't mix up versions, ABI breakages urrywhur */
	SDL_Log(
		"headerVersion: %X FMOD_VERSION: %X\n",
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
	SDL_assert(studioSystemCreate(system, headerVersion) == FMOD_OK);
	SDL_assert(studioSystemGetLowLevel(*system, &lowLevel) == FMOD_OK);
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
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	systemRegisterOutput(lowLevel, &FMOD_SDL_INTERNAL_Driver, &handle);
	systemSetOutputByPlugin(lowLevel, handle);
	/* mono needs this to leak :| SDL_UnloadObject(fmodlib); */

	#undef LOAD_FUNC

	/* We out. */
	SDL_Log("FMOD_SDL is registered!\n");
	return FMOD_OK;
}
#endif
