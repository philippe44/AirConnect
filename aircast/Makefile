ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

ifeq ($(findstring gcc,$(CC)),gcc)
CFLAGS  += -Wno-deprecated-declarations -Wno-format-truncation -Wno-stringop-truncation 
LDFLAGS += -s
else
CFLAGS += -fno-temp-file
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

ifneq ($(HOST),macos)
ifneq ($(HOST),solaris)
LINKSTATIC = -static -latomic
else
LDFLAGS += -lssp	
endif
endif

#ifneq ($(or $(filter %mips% %powerpc%,$(CC)),$(if $(findstring 64,$(CC)),,)))
#ifneq ($(or $(filter %mips% %powerpc%,$(HOST)),$(and $(findstring 64,$(PLATFORM)), $(findstring macos, $(PLATFORM)))),)
#LDFLAGS += -latomic
#endif	

BASE              = ..
CORE              = $(BASE)/bin/aircast-$(HOST)
BUILDDIR          = $(dir $(CORE))$(HOST)/$(PLATFORM)
EXECUTABLE        = $(CORE)-$(PLATFORM)
EXECUTABLE_STATIC = $(EXECUTABLE)-static

SRC		= src
TOOLS		= $(COMMON)/crosstools/src
COMMON		= $(BASE)/common
MDNS		= $(COMMON)/libmdns/targets
RAOP		= $(COMMON)/libraop/targets
#VALGRIND	= $(BASE)/valgrind
PUPNP 		= $(COMMON)/libpupnp/targets/$(HOST)/$(PLATFORM)
CODECS		= $(COMMON)/libcodecs/targets
OPENSSL		= $(COMMON)/libopenssl/targets/$(HOST)/$(PLATFORM)
NANOPB		= nanopb
JANSSON		= libjansson/targets/$(HOST)/$(PLATFORM)

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUPNP_STATIC_LIB
CFLAGS  	+= -Wall -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections -std=gnu11
LDFLAGS 	+= -lpthread -ldl -lm -L. 

vpath %.c $(TOOLS):$(SRC):$(NANOPB):$(COMMON)

INCLUDE = -I$(OPENSSL)/include \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC)/inc \
		  -I$(RAOP)/include \
		  -I$(PUPNP)/include/upnp -I$(PUPNP)/include/ixml -I$(PUPNP)/include/addons \
		  -I$(CODECS)/include/flac -I$(CODECS)/include/shine \
		  -I$(MDNS)/include/mdnssvc -I$(MDNS)/include/mdnssd \
		  -I$(NANOPB) \
		  -I$(JANSSON)/include
		  		  
DEPS	= $(SRC)/aircast.h $(LIBRARY) $(LIBRARY_STATIC)
				  
SOURCES = castcore.c castmessage.pb.c aircast.c cast_util.c cast_parse.c config_cast.c  \
	  cross_util.c cross_log.c cross_net.c cross_thread.c platform.c \
	  pb_common.c pb_decode.c pb_encode.c 
		
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
	echo $(PLATFORM)
	$(CC) $(OBJECTS) $(LIBRARY) $(CFLAGS) $(LDFLAGS) -o $@
ifeq ($(HOST),macos)
	rm -f $(CORE)
	lipo -create -output $(CORE) $$(ls $(CORE)* | grep -v '\-static')
endif

$(EXECUTABLE_STATIC): $(OBJECTS_STATIC)
	$(CC) $(OBJECTS_STATIC) $(LIBRARY_STATIC) $(CFLAGS) $(LDFLAGS) $(LINKSTATIC) -o $@
ifeq ($(HOST),macos)
	rm -f $(CORE)-static
	lipo -create -output $(CORE)-static $(CORE)-*-static
endif

$(OBJECTS) $(OBJECTS_STATIC): $(DEPS)

directory:
	@mkdir -p $(BUILDDIR)
	@mkdir -p bin

$(BUILDDIR)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@

$(BUILDDIR)/%-static.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSSL_STATIC_LIB $(INCLUDE) $< -c -o $(BUILDDIR)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(OBJECTS_STATIC) $(EXECUTABLE_STATIC) $(CORE) $(CORE)-static

