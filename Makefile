# Makefile for FMOD_SDL
# Written by Ethan "flibitijibibo" Lee

FMOD_VERSION = 10

all:
	$(CC) -O3 -Wall -pedantic -fpic -fPIC -shared -o libfmod_SDL.so FMOD_SDL.c `sdl2-config --cflags --libs` libfmod.so.$(FMOD_VERSION)
