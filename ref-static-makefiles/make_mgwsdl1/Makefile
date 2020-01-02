# makefile for mingw SDL1
#   gcc version 6.3

CC		=	gcc
CXX		=   g++
STRIP	=	strip
RM		=	rm -f

DBX_PATH	= ../..

SDLDIR = ../../vs2015/sdl
ZLIB = ../../vs2015/zlib
LPNG = ../../vs2015/libpng

MT32 = 1

CFLAGS	=	-O3 -DNDEBUG -Wstrict-overflow=0 -Wstrict-aliasing=0 -fomit-frame-pointer  -mno-ms-bitfields  -I. \
			-I$(DBX_PATH) \
			-I$(DBX_PATH)/include \
			-I$(DBX_PATH)/src \
			-I$(DBX_PATH)/src/aviwriter \
			-I$(DBX_PATH)/src/hardware/snd_pc98/sound \
			-I$(DBX_PATH)/src/hardware/snd_pc98/common \
			-I$(DBX_PATH)/src/hardware/snd_pc98/generic \
			-I$(DBX_PATH)/src/hardware/snd_pc98/sound/getsnd \
			-I$(DBX_PATH)/src/hardware/snd_pc98/x11 \
			-I$(DBX_PATH)/src/hardware/snd_pc98/cbus 
			
CFLAGS	+=  -DNO_STDIO_REDIRECT  -I$(SDLDIR)/include -I$(LPNG) -I$(ZLIB)

CXXFLAGS = -O3  -DNDEBUG -std=gnu++11 -Wstrict-overflow=0 -Wstrict-aliasing=0 -fomit-frame-pointer  -mno-ms-bitfields  -I. \
			-I$(DBX_PATH) \
			-I$(DBX_PATH)/include \
			-I$(DBX_PATH)/src \
			-I$(DBX_PATH)/src/aviwriter \
			-I$(DBX_PATH)/src/hardware/snd_pc98/sound \
			-I$(DBX_PATH)/src/hardware/snd_pc98/common \
			-I$(DBX_PATH)/src/hardware/snd_pc98/generic \
			-I$(DBX_PATH)/src/hardware/snd_pc98/sound/getsnd \
			-I$(DBX_PATH)/src/hardware/snd_pc98/x11 \
			-I$(DBX_PATH)/src/hardware/snd_pc98/cbus 
			
CXXFLAGS	+=	-Dmain=SDL_main -I$(SDLDIR)/include -I$(LPNG) -I$(ZLIB)

LFLAGS	=

TARGET	=	dosbox-x.exe

SDLSRCS	=	$(SDLDIR)/src/SDL.c \
			$(SDLDIR)/src/SDL_error.c \
			$(SDLDIR)/src/SDL_fatal.c \
			$(SDLDIR)/src/audio/SDL_audio.c \
			$(SDLDIR)/src/audio/SDL_audiocvt.c \
			$(SDLDIR)/src/audio/SDL_audiodev.c \
			$(SDLDIR)/src/audio/SDL_mixer.c \
			$(SDLDIR)/src/audio/SDL_mixer_MMX_VC.c \
			$(SDLDIR)/src/audio/SDL_wave.c \
			$(SDLDIR)/src/audio/disk/SDL_diskaudio.c \
			$(SDLDIR)/src/audio/dummy/SDL_dummyaudio.c \
			$(SDLDIR)/src/audio/windib/SDL_dibaudio.c \
			$(SDLDIR)/src/audio/windx5/SDL_dx5audio.c \
			$(SDLDIR)/src/cdrom/SDL_cdrom.c \
			$(SDLDIR)/src/cdrom/win32/SDL_syscdrom.c \
			$(SDLDIR)/src/cpuinfo/SDL_cpuinfo.c \
			$(SDLDIR)/src/events/SDL_active.c \
			$(SDLDIR)/src/events/SDL_events.c \
			$(SDLDIR)/src/events/SDL_expose.c \
			$(SDLDIR)/src/events/SDL_keyboard.c \
			$(SDLDIR)/src/events/SDL_mouse.c \
			$(SDLDIR)/src/events/SDL_quit.c \
			$(SDLDIR)/src/events/SDL_resize.c \
			$(SDLDIR)/src/file/SDL_rwops.c \
			$(SDLDIR)/src/joystick/SDL_joystick.c \
			$(SDLDIR)/src/joystick/win32/SDL_mmjoystick.c \
			$(SDLDIR)/src/loadso/win32/SDL_sysloadso.c \
			$(SDLDIR)/src/stdlib/SDL_getenv.c \
			$(SDLDIR)/src/stdlib/SDL_iconv.c \
			$(SDLDIR)/src/stdlib/SDL_malloc.c \
			$(SDLDIR)/src/stdlib/SDL_qsort.c \
			$(SDLDIR)/src/stdlib/SDL_stdlib.c \
			$(SDLDIR)/src/stdlib/SDL_string.c \
			$(SDLDIR)/src/thread/SDL_thread.c \
			$(SDLDIR)/src/thread/win32/SDL_sysmutex.c \
			$(SDLDIR)/src/thread/win32/SDL_syssem.c \
			$(SDLDIR)/src/thread/win32/SDL_systhread.c \
			$(SDLDIR)/src/timer/SDL_timer.c \
			$(SDLDIR)/src/timer/win32/SDL_systimer.c \
			$(SDLDIR)/src/video/SDL_blit.c \
			$(SDLDIR)/src/video/SDL_blit_0.c \
			$(SDLDIR)/src/video/SDL_blit_1.c \
			$(SDLDIR)/src/video/SDL_blit_A.c \
			$(SDLDIR)/src/video/SDL_blit_N.c \
			$(SDLDIR)/src/video/SDL_bmp.c \
			$(SDLDIR)/src/video/SDL_cursor.c \
			$(SDLDIR)/src/video/SDL_gamma.c \
			$(SDLDIR)/src/video/SDL_pixels.c \
			$(SDLDIR)/src/video/SDL_RLEaccel.c \
			$(SDLDIR)/src/video/SDL_stretch.c \
			$(SDLDIR)/src/video/SDL_surface.c \
			$(SDLDIR)/src/video/SDL_video.c \
			$(SDLDIR)/src/video/SDL_yuv.c \
			$(SDLDIR)/src/video/SDL_yuv_sw.c \
			$(SDLDIR)/src/video/SDL_yuv_mmx.c \
			$(SDLDIR)/src/video/dummy/SDL_nullevents.c \
			$(SDLDIR)/src/video/dummy/SDL_nullmouse.c \
			$(SDLDIR)/src/video/dummy/SDL_nullvideo.c \
			$(SDLDIR)/src/video/wincommon/SDL_sysevents.c \
			$(SDLDIR)/src/video/wincommon/SDL_sysmouse.c \
			$(SDLDIR)/src/video/wincommon/SDL_syswm.c \
			$(SDLDIR)/src/video/wincommon/SDL_wingl.c \
			$(SDLDIR)/src/video/windib/SDL_dibvideo.c \
			$(SDLDIR)/src/video/windib/SDL_dibevents.c \
			$(SDLDIR)/src/video/windib/SDL_multimon.c \
			$(SDLDIR)/src/video/windx5/SDL_dx5video.c \
			$(SDLDIR)/src/video/windx5/SDL_dx5events.c \
			$(SDLDIR)/src/video/windx5/SDL_dx5yuv.c	
 
ifdef WINDIR
	SDLSRCS += $(SDLDIR)/src/main/win32/SDL_win32_main.obj
endif

ZLIBSRCS	=	$(ZLIB)/adler32.c \
			$(ZLIB)/compress.c \
			$(ZLIB)/crc32.c \
			$(ZLIB)/deflate.c \
			$(ZLIB)/gzclose.c \
			$(ZLIB)/gzlib.c \
			$(ZLIB)/gzread.c \
			$(ZLIB)/gzwrite.c \
			$(ZLIB)/infback.c \
			$(ZLIB)/inffast.c \
			$(ZLIB)/inflate.c \
			$(ZLIB)/inftrees.c \
			$(ZLIB)/trees.c \
			$(ZLIB)/uncompr.c \
			$(ZLIB)/zutil.c

PNGSRCS	=	$(LPNG)/png.c \
			$(LPNG)/pngerror.c \
			$(LPNG)/pngget.c \
			$(LPNG)/pngmem.c \
			$(LPNG)/pngpread.c \
			$(LPNG)/pngread.c \
			$(LPNG)/pngrio.c \
			$(LPNG)/pngrtran.c \
			$(LPNG)/pngrutil.c \
			$(LPNG)/pngset.c \
			$(LPNG)/pngtrans.c \
			$(LPNG)/pngwio.c \
			$(LPNG)/pngwrite.c \
			$(LPNG)/pngwtran.c \
			$(LPNG)/pngwutil.c

CPPSRCS	=	$(DBX_PATH)/src/dosbox.cpp \
			$(wildcard $(DBX_PATH)/src/aviwriter/*.cpp) \
			$(wildcard $(DBX_PATH)/src/builtin/*.cpp) \
			$(wildcard $(DBX_PATH)/src/cpu/*.cpp) \
			$(wildcard $(DBX_PATH)/src/debug/*.cpp) \
			$(wildcard $(DBX_PATH)/src/dos/*.cpp) \
			$(wildcard $(DBX_PATH)/src/fpu/*.cpp) \
			$(wildcard $(DBX_PATH)/src/gui/*.cpp) \
			$(wildcard $(DBX_PATH)/src/gui/*.c) \
			$(wildcard $(DBX_PATH)/src/hardware/*.cpp) \
			$(wildcard $(DBX_PATH)/src/hardware/parport/*.cpp) \
			$(wildcard $(DBX_PATH)/src/hardware/serialport/*.cpp) \
			$(DBX_PATH)/src/hardware/snd_pc98/cbus/board86.c \
			$(DBX_PATH)/src/hardware/snd_pc98/cbus/board26k.c \
			$(wildcard $(DBX_PATH)/src/hardware/snd_pc98/common/*.c) \
			$(wildcard $(DBX_PATH)/src/hardware/snd_pc98/generic/*.c) \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/opngeng.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/opngenc.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/pcm86g.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/pcm86c.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/tms3631g.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/tms3631c.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/psggeng.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/psggenc.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/adpcmg.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/adpcmc.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/rhythmc.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/fmboard.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/fmtimer.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/sound.c \
			$(DBX_PATH)/src/hardware/snd_pc98/sound/soundrom.c \
			$(wildcard $(DBX_PATH)/src/hardware/snd_pc98/sound/getsnd/*.c) \
			$(wildcard $(DBX_PATH)/src/hardware/snd_pc98/x11/*.c) \
			$(wildcard $(DBX_PATH)/src/ints/*.cpp) \
			$(wildcard $(DBX_PATH)/src/libs/gui_tk/*.cpp) \
			$(wildcard $(DBX_PATH)/src/libs/zmbv/*.cpp) \
			$(wildcard $(DBX_PATH)/src/misc/*.cpp) \
			$(wildcard $(DBX_PATH)/src/output/*.cpp) \
			$(wildcard $(DBX_PATH)/src/output/direct3d/*.cpp) \
			$(wildcard $(DBX_PATH)/src/shell/*.cpp) 

CPPSRCS_MT32 = $(wildcard $(DBX_PATH)/src/mt32/*.cpp) \
			$(wildcard $(DBX_PATH)/src/mt32/freeverb/*.cpp) \
			$(wildcard $(DBX_PATH)/src/mt32/sha1/*.cpp)

OBJS_MT32 = $(addsuffix .o,$(basename $(CPPSRCS_MT32)))

CXXFLAGS_MT32 = -DC_MT32=1 -I$(DBX_PATH)/src/mt32 -I$(DBX_PATH)/src/mt32/freeverb -I$(DBX_PATH)/src/mt32/sha1

OBJS_SDL = $(addsuffix .o,$(basename $(SDLSRCS)))
OBJS_ZLIB = $(addsuffix .o,$(basename $(ZLIBSRCS)))
OBJS_PNG = $(addsuffix .o,$(basename $(PNGSRCS)))
OBJS = $(addsuffix .o,$(basename $(CPPSRCS)))
LIBS = -static -lm -lstdc++ -mwindows -lkernel32 -luser32 -lgdi32 -limm32 -lole32 -lwinmm -ldxguid -lshell32 -lopengl32 -ladvapi32 -lws2_32

ifeq ($(OPENGL), 1)
	CXXFLAGS += -DC_OPENGL=1
	ifdef WINDIR
		LIBS += -lopengl32
	else
		LIBS += -lGL
	endif
endif

ifeq ($(MT32),1)
	CPPSRCS += $(CPPSRCS_MT32)
	CXXFLAGS += $(CXXFLAGS_MT32)
#	OBJS += $(OBJS_MT32)
endif

.SUFFIXES: .c.o
.SUFFIXES: .cpp.o

all:	$(TARGET)

$(TARGET):	$(OBJS_SDL) $(OBJS_ZLIB) $(OBJS_PNG) $(OBJS)
	$(CC) $(LFLAGS) -g -o $@ $(OBJS_SDL) $(OBJS_ZLIB) $(OBJS_PNG) $(OBJS) $(LIBS) 
	$(STRIP) $@

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS_SDL) $(OBJS_ZLIB) $(OBJS_PNG) $(OBJS) $(OBJS_MT32)
