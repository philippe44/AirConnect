ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

BASE = ..

BUILDDIR	= build/$(PLATFORM)
EXECUTABLE	= $(BASE)/bin/aircast-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/aircast-$(PLATFORM)-static

SRC			= src
TOOLS		= $(COMMON)/crosstools/src
COMMON		= $(BASE)/common
MDNS		= $(COMMON)/libmdns/targets
RAOP		= $(COMMON)/raoptools/targets
#VALGRIND	= $(BASE)/valgrind
PUPNP 		= $(COMMON)/libpupnp/targets/$(HOST)/$(PLATFORM)
CODECS		= $(COMMON)/libcodecs/targets
OPENSSL		= $(COMMON)/libopenssl/targets/$(HOST)/$(PLATFORM)
NANOPB		= nanopb
JANSSON		= libjansson/targets/$(HOST)/$(PLATFORM)

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUPNP_STATIC_LIB
CFLAGS  	+= -Wall -Wno-deprecated-declarations -Wno-format-truncation -Wno-stringop-truncation -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS 	+= -s -lpthread -ldl -lm -lrt -L. 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(SRC):$(NANOPB)

INCLUDE = -I$(OPENSSL)/include \
		  -I$(TOOLS) \
		  -I$(SRC)/inc \
		  -I$(RAOP)/include \
		  -I$(PUPNP)/include/upnp -I$(PUPNP)/include/ixml -I$(PUPNP)/include/addons \
		  -I$(CODECS)/include/flac -I$(CODECS)/include/shine \
		  -I$(MDNS)/include/tinysvcmdns -I$(MDNS)/include/mdnssd \
		  -I$(NANOPB) \
		  -I$(JANSSON)/include
		  		  
DEPS	= $(SRC)/aircast.h
				  
SOURCES = castcore.c castmessage.pb.c aircast.c cast_util.c cast_parse.c config_cast.c  \
	  cross_util.c cross_log.c cross_net.c cross_thread.c platform.c \
	  pb_common.c pb_decode.c pb_encode.c \
		
SOURCES_LIBS = cross_ssl.c
		
OBJECTS 		= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES) $(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES)) $(patsubst %.c,$(BUILDDIR)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY		= $(RAOP)/$(HOST)/$(PLATFORM)/libraop.a \
			  $(PUPNP)/libpupnp.a \
			  $(CODECS)/$(HOST)/$(PLATFORM)/libcodecs.a \
			  $(MDNS)/$(HOST)/$(PLATFORM)/libmdns.a \
			  $(JANSSON)/libjansson.a
				  
LIBRARY_STATIC 	= $(LIBRARY) $(OPENSSL)/libopenssl.a

all: directory $(EXECUTABLE) $(EXECUTABLE_STATIC)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@

$(EXECUTABLE_STATIC): $(OBJECTS_STATIC)
	$(CC) $(OBJECTS_STATIC) $(LIBRARY_STATIC) $(LDFLAGS) -static -o $@	

directory:
	@mkdir -p $(BUILDDIR)
	@mkdir -p bin

$(BUILDDIR)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@

$(BUILDDIR)/%-static.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DLINKALL $(INCLUDE) $< -c -o $(BUILDDIR)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(OBJECTS_STATIC) $(EXECUTABLE_STATIC) 

