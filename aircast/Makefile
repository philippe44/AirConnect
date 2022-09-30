BASE = ..

OBJ			= build/$(PLATFORM)
EXECUTABLE	= $(BASE)/bin/aircast-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/aircast-$(PLATFORM)-static

TOOLS		= $(BASE)/tools
COMMON		= $(BASE)/common
SRC			= ./src
MDNSSD		= $(COMMON)/mdns-sd
TINYSVCMDNS	= $(COMMON)/tinysvcmdns
#VALGRIND	= $(BASE)/valgrind
UPNP 		= $(COMMON)/pupnp
SHINE		= $(COMMON)/shine
FLAC		= $(COMMON)/flac
OGG			= $(COMMON)/ogg
DMAP_PARSER	= $(COMMON)/dmap-parser
OPENSSL		= $(COMMON)/openssl
NANOPB		= ./nanopb
JANSSON		= ./jansson

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUSE_XML
CFLAGS  	+= -Wall -Wno-format-truncation -fPIC -ggdb -O2 $(OPTS) $(INCLUDE) $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS 	+= -lpthread -ldl -lm -lrt -L. 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(SQUEEZETINY):$(NANOPB):$(MDNSSD):$(TINYSVCMDNS):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(OPENSSL)/targets/linux/$(PLATFORM)/include \
		  -I$(SHINE)/src/lib \
		  -I$(DMAP_PARSER) \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(UPNP)/ixml/inc -I$(UPNP)/upnp/inc -I$(UPNP)/targets/linux/$(PLATFORM)/include -I$(UPNP)/targets/include \
		  -I$(MDNSSD) \
		  -I$(FLAC)/include\
		  -I$(TINYSVCMDNS) \
		  -I$(NANOPB) \
		  -I$(JANSSON)/targets/linux/include
		  		  
DEPS	= $(SRC)/aircast.h
				  
SOURCES = castcore.c castmessage.pb.c aircast.c cast_util.c cast_parse.c config_cast.c  \
		  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c \
		  pb_common.c pb_decode.c pb_encode.c \
		  dmap_parser.c \
	  	  mdnssd-min.c
		
SOURCES_LIBS = sslsym.c
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(UPNP)/targets/linux/$(PLATFORM)/libixml.a $(UPNP)/targets/linux/$(PLATFORM)/libupnp.a \
				  $(FLAC)/targets/linux/$(PLATFORM)/libFLAC.a  $(OGG)/targets/linux/$(PLATFORM)/libogg.a \
				  $(SHINE)/targets/linux/$(PLATFORM)/libshine.a \
				  $(TINYSVCMDNS)/targets/linux/$(PLATFORM)/tinysvcmdns.a \
				  $(JANSSON)/targets/linux/$(PLATFORM)/libjansson.a
				  
LIBRARY_STATIC 	= $(LIBRARY) $(COMMON)/openssl/targets/linux/$(PLATFORM)/libssl.a $(LIBRARY) $(COMMON)/openssl/targets/linux/$(PLATFORM)/libcrypto.a

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

