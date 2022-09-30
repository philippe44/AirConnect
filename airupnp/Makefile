BASE		= ..

OBJ					= bin/$(PLATFORM)
EXECUTABLE 			= $(BASE)/bin/airupnp-$(PLATFORM)
EXECUTABLE_STATIC	= $(BASE)/bin/airupnp-$(PLATFORM)-static

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

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DNDEBUG -D_GNU_SOURCE -DUSE_XML
CFLAGS 		+= -fdata-sections -ffunction-sections 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(SQUEEZETINY):$(MDNSSD):$(TINYSVCMDNS):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(OPENSSL)/targets/linux/$(PLATFORM)/include \
		  -I$(SHINE)/src/lib \
		  -I$(DMAP_PARSER) \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(UPNP)/ixml/inc -I$(UPNP)/upnp/inc -I$(UPNP)/targets/inc -I$(UPNP)/targets/linux/$(PLATFORM)/inc	\
		  -I$(MDNSSD) \
		  -I$(FLAC)/include\
		  -I$(TINYSVCMDNS)
		  		  
DEPS	= $(SRC)/airupnp.h
				  
SOURCES = avt_util.c airupnp.c mr_util.c config_upnp.c  \
		  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c \
		  dmap_parser.c \
		  mdnssd-min.c

SOURCES_LIBS = sslsym.c
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(UPNP)/targets/linux/$(PLATFORM)/libixml.a $(UPNP)/targets/linux/$(PLATFORM)/libupnp.a \
				  $(FLAC)/targets/linux/$(PLATFORM)/libFLAC.a  $(OGG)/targets/linux/$(PLATFORM)/libogg.a \
				  $(SHINE)/targets/linux/$(PLATFORM)/libshine.a \
				  $(TINYSVCMDNS)/targets/linux/$(PLATFORM)/tinysvcmdns.a
				  
#LIBRARY_STATIC 	= $(LIBRARY) -lcrypto
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
