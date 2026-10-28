#
# Nikwi Deluxe makefile. Needs GNU Make
#

OUTPUT = nikwi
SOURCES = $(wildcard src/nikwi/*.cpp) src/slashfx/main.c $(wildcard src/slashtdp/*.cpp) $(wildcard src/us/*.cpp) src/badcfg/main.c
# SF3500 hw bridge files (hwdisp/hwaudio/joykey and helpers). These are .c (not .cpp)
# on purpose: they must be compiled with $(CC)/gcc, never g++, since
# hwaudio.c uses `try` as a variable name (a C++ keyword) - the implicit
# make pattern rules already route .c through $(CC) correctly.
ifdef SF3500
SOURCES += $(wildcard src/hw/*.c)
endif
OBJECTS = $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCES)))
HEADERS = $(wildcard src/nikwi/*.h) src/slashfx/slashfx.h $(wildcard src/slashtdp/slashtdp/*.h) $(wildcard src/us/uscript/*.h) src/badcfg/badcfg.h
ifdef SF3500
LIBS = -lSDL -lpthread -ldl -lm
else
ifeq ($(shell uname -s),Darwin)
LIBS = -m32 -framework SDL -framework Cocoa
else
LIBS = -lSDL
endif
ifeq ($(shell uname -s | sed s/[0-9]*_.*//),MINGW)
LIBS := $(LIBS) -mwindows -static-libgcc `$(CXX) -print-file-name=libstdc++.a`
OBJECTS := $(OBJECTS) nikwi.res
endif
endif
ifdef SF3500
# Optimised build: without this the game, its software drawing and its audio
# mixer all run unoptimised. -fno-strict-aliasing because this 2005 code casts
# between pixel and sample pointer types freely.
CFLAGS := -DSF3500 -O2 -fno-strict-aliasing $(CFLAGS)
endif
CFLAGS := -g3 -Wall -Wno-write-strings -Isrc/badcfg -Isrc/nikwi -Isrc/slashfx -Isrc/slashtdp -Isrc/us -Isrc $(CFLAGS) $(XCFLAGS) $(CPPFLAGS)
CXXFLAGS = $(CFLAGS)
ifeq ($(shell uname -s),Darwin)
CFLAGS := -m32 $(CFLAGS) -I/Library/Frameworks/SDL.framework/Headers
endif

.PHONY: all
all: game data

.PHONY: game
game: $(OUTPUT)

.PHONY: data
data: tools justdata.up

.PHONY: tools
tools: src/tools/bmp2ut/bmp2ut src/tools/upack/upack

src/tools/bmp2ut/bmp2ut: src/tools/bmp2ut/bmp2ut.c
ifeq ($(shell uname -s),Darwin)
	$(CC) -o $@ $< -framework SDL -framework Cocoa -I/Library/Frameworks/SDL.framework/Headers
else
	$(CC) -o $@ $< -lSDL
endif

src/tools/upack/upack: src/tools/upack/upack.c
	$(CC) -o $@ $<

ifeq ($(shell uname -s),Darwin)
OBJECTS := $(OBJECTS) src/nikwi/osx/SDLMain.o
src/nikwi/osx/SDLMain.o: src/nikwi/osx/SDLMain.m
	$(CC) $(CFLAGS) -c -o $@ $< -m32 -framework SDL -framework Cocoa
endif

justdata.up: tools
	./makedata.sh
	./makepack.sh

nikwi.res: nikwi.rc nikwi.ico
	windres -i nikwi.rc --input-format=rc -o nikwi.res -O coff

$(OUTPUT): $(OBJECTS)
	$(CXX) -o $(OUTPUT) $(OBJECTS) $(USELDFLAGS) $(LIBS)

.PHONY: clean
clean:
	$(RM) -fr $(OBJECTS) $(OUTPUT) $(OUTPUT).exe
	$(RM) -fr src/tools/bmp2ut/bmp2ut src/tools/bmp2ut/bmp2ut.exe
	$(RM) -fr src/tools/upack/upack src/tools/upack/upack.exe
	$(RM) -fr justdata.up nikwi.res
	$(RM) -fr images/tiles/*.ut images/objects/*.ut data/tiles/*.ut data/objects/*.ut

