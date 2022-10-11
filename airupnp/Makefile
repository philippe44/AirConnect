ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

BASE		= ..

BUILDDIR	= build/$(PLATFORM)
EXECUTABLE 	= $(BASE)/bin/airupnp-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/airupnp-$(PLATFORM)-static

TOOLS		= $(BASE)/tools
COMMON		= $(BASE)/common
SRC			= src
MDNS		= $(COMMON)/libmdns/targets
#VALGRIND	= $(BASE)/valgrind
UPNP 		= $(COMMON)/libpupnp/targets/$(HOST)/$(PLATFORM)
CODECS		= $(COMMON)/libcodecs/targets
DMAP_PARSER	= $(COMMON)/dmap-parser
OPENSSL		= $(COMMON)/libopenssl/targets/$(HOST)/$(PLATFORM)

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE
CFLAGS  	+= -Wall -Wno-deprecated-declarations -Wno-format-truncation -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS 	+= -s -lpthread -ldl -lm -lrt -L. 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(OPENSSL)/include \
		  -I$(CODECS)/include/flac \
		  -I$(CODECS)/include/shine \
		  -I$(DMAP_PARSER) \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC)/inc \
		  -I$(UPNP)/include \
		  -I$(MDNS)/include/tinysvcmdns \
		  -I$(MDNS)/include/mdnssd 
		  		  
DEPS	= $(SRC)/airupnp.h
				  
SOURCES = avt_util.c airupnp.c mr_util.c config_upnp.c  \
	  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c platform.c \
	  dmap_parser.c

SOURCES_LIBS = sslsym.c
		
OBJECTS 		= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES) $(SOURCES_LIBS))
OBJECTS_STATIC 	= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES)) $(patsubst %.c,$(BUILDDIR)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(UPNP)/libpupnp.a $(CODECS)/$(HOST)/$(PLATFORM)/libcodecs.a $(MDNS)/$(HOST)/$(PLATFORM)/libmdns.a  
LIBRARY_STATIC 	= $(LIBRARY) $(OPENSSL)/libopenssl.a

all: directory $(EXECUTABLE) $(EXECUTABLE_STATIC)
		
$(EXECUTABLE): $(OBJECTS) $(DEPS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@
	
$(EXECUTABLE_STATIC): $(OBJECTS_STATIC) $(DEPS)
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
