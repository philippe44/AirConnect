ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

BASE		= ..

OBJ			= build/$(PLATFORM)
EXECUTABLE 	= $(BASE)/bin/airupnp-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/airupnp-$(PLATFORM)-static

TOOLS		= $(BASE)/tools
COMMON		= $(BASE)/common
SRC		= ./src
MDNS		= $(COMMON)/libmdns
#VALGRIND	= $(BASE)/valgrind
UPNP 		= $(COMMON)/libpupnp
SHINE		= $(COMMON)/shine
FLAC		= $(COMMON)/flac
OGG		= $(COMMON)/ogg
DMAP_PARSER	= $(COMMON)/dmap-parser
OPENSSL		= $(COMMON)/libopenssl

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUSE_XML
CFLAGS  	+= -Wall -Wno-deprecated-declarations -Wno-format-truncation -fPIC -ggdb -O2 $(OPTS) $(INCLUDE) $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS 	+= -s -lpthread -ldl -lm -lrt -L. 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(SQUEEZETINY):$(MDNSSD):$(TINYSVCMDNS):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(OPENSSL)/targets/$(HOST)/$(PLATFORM)/include \
		  -I$(SHINE)/targets/include \
		  -I$(DMAP_PARSER) \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(UPNP)/targets/$(HOST)/$(PLATFORM)/include \
		  -I$(FLAC)/targets/include\
		  -I$(MDNS)/targets/include/tinysvcmdns \
		  -I$(MDNS)/targets/include/mdnssd \

		  		  
DEPS	= $(SRC)/airupnp.h
				  
SOURCES = avt_util.c airupnp.c mr_util.c config_upnp.c  \
	  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c platform.c \
	  dmap_parser.c

SOURCES_LIBS = sslsym.c
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(UPNP)/targets/$(HOST)/$(PLATFORM)/libpupnp.a \
			  $(FLAC)/targets/$(HOST)/$(PLATFORM)/libFLAC-static.a $(OGG)/targets/$(HOST)/$(PLATFORM)/libogg.a \
			  $(SHINE)/targets/$(HOST)/$(PLATFORM)/libshine.a \
			  $(MDNS)/targets/$(HOST)/$(PLATFORM)/libmdns.a
				  
LIBRARY_STATIC 	= $(LIBRARY) $(COMMON)/libopenssl/targets/$(HOST)/$(PLATFORM)/libopenssl.a

all: $(EXECUTABLE) $(EXECUTABLE_STATIC)
		
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@
	
$(EXECUTABLE_STATIC): $(OBJECTS_STATIC)
	$(CC) $(OBJECTS_STATIC) $(LIBRARY_STATIC) $(LDFLAGS) -static -o $@	

$(OBJECTS): $(DEPS) | bin $(OBJ) 

$(OBJ):
	@mkdir -p $@
	
bin:	
	@mkdir -p bin

$(OBJ)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@

$(OBJ)/%-static.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DLINKALL $(INCLUDE) $< -c -o $(OBJ)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(OBJECTS_STATIC) $(EXECUTABLE_STATIC) 
