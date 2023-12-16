TARGET = ../../build/TF-EhpLoader
# include more objs here depending on what you use (e.g. "../../includes/psp/mips.o")
OBJS = main.o ehploader.o exports.o ../../includes/psp/pspmallochelper.o ../../includes/psp/minjector.o ../../includes/psp/patterns.o

DEFINES = -DMINJECTOR_MINIMAL

CFLAGS = -O2 -Os -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division -fpack-struct=16 $(DEFINES)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

LIBS = -lpspsystemctrl_user -lpspkubridge -lm

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
