ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

BASE		= ..

BUILDDIR	= build/$(PLATFORM)
EXECUTABLE 	= $(BASE)/bin/airupnp-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/airupnp-$(PLATFORM)-static

SRC			= src
COMMON		= $(BASE)/common
TOOLS		= $(COMMON)/crosstools/src
RAOP		= $(COMMON)/raoptools/targets
MDNS		= $(COMMON)/libmdns/targets
#VALGRIND	= $(BASE)/valgrind
PUPNP 		= $(COMMON)/libpupnp/targets/$(HOST)/$(PLATFORM)
CODECS		= $(COMMON)/libcodecs/targets
OPENSSL		= $(COMMON)/libopenssl/targets/$(HOST)/$(PLATFORM)

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUPNP_STATIC_LIB
CFLAGS  	+= -Wall -Wno-deprecated-declarations -Wno-format-truncation -Wno-stringop-truncation -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS 	+= -s -lpthread -ldl -lm -lrt -L. 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(SRC):$(COMMON)

INCLUDE = -I$(OPENSSL)/include \
	  -I$(COMMON) \
		  -I$(CODECS)/include/flac -I$(CODECS)/include/shine \
		  -I$(TOOLS) \
		  -I$(SRC)/inc \
		  -I$(RAOP)/include \
		  -I$(PUPNP)/include/upnp -I$(PUPNP)/include/ixml -I$(PUPNP)/include/addons \
		  -I$(MDNS)/include/tinysvcmdns -I$(MDNS)/include/mdnssd 
		  		  
DEPS	= $(SRC)/airupnp.h
				  
SOURCES = avt_util.c airupnp.c mr_util.c config_upnp.c  \
	  cross_util.c cross_log.c cross_net.c cross_thread.c platform.c 

SOURCES_LIBS = cross_ssl.c
		
OBJECTS		= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES) $(SOURCES_LIBS))
OBJECTS_STATIC 	= $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES)) $(patsubst %.c,$(BUILDDIR)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY	= $(RAOP)/$(HOST)/$(PLATFORM)/libraop.a \
			$(PUPNP)/libpupnp.a \
			$(CODECS)/$(HOST)/$(PLATFORM)/libcodecs.a \
			$(MDNS)/$(HOST)/$(PLATFORM)/libmdns.a  
			
LIBRARY_STATIC = $(LIBRARY) $(OPENSSL)/libopenssl.a

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
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSSL_LIB_STATIC $(INCLUDE) $< -c -o $(BUILDDIR)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(OBJECTS_STATIC) $(EXECUTABLE_STATIC) 
