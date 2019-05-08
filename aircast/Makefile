BASE		= ../..

TOOLS		= ../tools
COMMON		= ../common
SRC			= ./src
JANSSON		= $(BASE)/jansson
NANOPB		= $(BASE)/nanopb
MDNSSD		= $(BASE)/mdnssd
TINYSVCMDNS	= $(BASE)/tinysvcmdns
VALGRIND	= $(BASE)/valgrind
UPNP 		= $(BASE)/libupnp/1.6.19
SHINE		= $(BASE)/shine/src/lib
DMAP_PARSER	= $(BASE)/dmap-parser

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DHAVE_STDINT_H -DNDEBUG -D_GNU_SOURCE -D_USE_XML_

CFLAGS 		+= -fdata-sections -ffunction-sections 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(SQUEEZETINY):$(JANSSON)/src:$(NANOPB):$(MDNSSD):$(TINYSVCMDNS):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(SHINE) \
		  -I$(DMAP_PARSER) \
		  -I$(VALGRIND)/memcheck -I$(VALGRIND)/include \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(UPNP)/ixml/inc -I$(UPNP)/upnp/inc -I$(UPNP)/build/inc -I$(UPNP)/threadutil/inc \
		  -I$(JANSSON)/src \
		  -I$(NANOPB) \
		  -I$(MDNSSD) \
		  -I$(BASE)/flac/include\
		  -I$(TINYSVCMDNS)
		  		  
DEPS	= $(SRC)/aircast.h
				  
SOURCES = castcore.c castmessage.pb.c aircast.c cast_util.c cast_parse.c config_cast.c  \
		  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c \
		  pb_common.c pb_decode.c pb_encode.c dump.c error.c hashtable.c strconv.c \
		  hashtable_seed.c load.c memory.c pack_unpack.c strbuffer.c utf.c value.c \
		  dmap_parser.c \
	  	  mdnssd-min.c \
		  mdns.c mdnsd.c \
		  sslsym.c
		
SOURCES_LIBS = 
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(OBJ)/libixml.a $(OBJ)/libFLAC.a $(OBJ)/libshine.a
LIBRARY_STATIC 	= $(LIBRARY)

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

