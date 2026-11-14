TARGET = GTRemastered
OBJS = main.o hooking.o disc_ident.o udp_receiver.o

# Ini penting, biar sceNetInet* ke-link
LIBS = -lpspnet_inet -lpspnet -lpspctrl -lpspkernel -lpspsystemctrl_user

CFLAGS = -O2 -G0 -Wall -fno-pic
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_FW_VERSION = 500

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
