# Makefile for FMOD_SDL
# Written by Ethan "flibitijibibo" Lee

FMOD_VERSION = 13

all:
	$(CC) -O3 -Wall -pedantic -fpic -fPIC -shared -o libfmod_SDL.so FMOD_SDL.c -lSDL3 libfmod.so.$(FMOD_VERSION)

preload:
	$(CC) -O3 -Wall -pedantic -fpic -fPIC -shared -o libfmod_SDL.so FMOD_SDL.c -DPRELOAD_MODE -lSDL3
