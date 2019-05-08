BASE		= ../..

TOOLS		= ../tools
COMMON		= ../common
SRC			= ./src
MDNSSD		= $(BASE)/mdnssd
TINYSVCMDNS	= $(BASE)/tinysvcmdns
VALGRIND	= $(BASE)/valgrind
UPNP 		= $(BASE)/libupnp/1.6.19
JANSSON		= $(BASE)/jansson
SHINE		= $(BASE)/shine/src/lib
DMAP_PARSER	= $(BASE)/dmap-parser

DEFINES 	+= -D_FILE_OFFSET_BITS=64 -DPB_FIELD_16BIT -DHAVE_STDINT_H -DNDEBUG -D_GNU_SOURCE -D_USE_XML_

CFLAGS 		+= -fdata-sections -ffunction-sections 

ifeq ($(OS),Darwin)
LDFLAGS 	+= -Wl,-dead_strip
else ifneq ($(OS), SunOS)
LDFLAGS 	+= -Wl,--gc-sections
endif

vpath %.c $(TOOLS):$(COMMON):$(SRC):$(SQUEEZETINY):$(MDNSSD):$(TINYSVCMDNS):$(DMAP_PARSER)

INCLUDE = -I. \
		  -I$(SHINE) \
		  -I$(DMAP_PARSER) \
		  -I$(VALGRIND)/memcheck -I$(VALGRIND)/include \
		  -I$(TOOLS) \
		  -I$(COMMON) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(UPNP)/ixml/inc -I$(UPNP)/upnp/inc -I$(UPNP)/build/inc -I$(UPNP)/threadutil/inc \
		  -I$(MDNSSD) \
		  -I$(JANSSON)/src \
		  -I$(BASE)/flac/include\
		  -I$(TINYSVCMDNS)
		  		  
DEPS	= $(SRC)/airupnp.h
				  
SOURCES = avt_util.c airupnp.c mr_util.c config_upnp.c  \
		  alac.c base64.c hairtunes.c raopcore.c util.c log_util.c \
		  dmap_parser.c \
		  mdnssd-min.c \
		  mdns.c mdnsd.c \
		  sslsym.c

SOURCES_LIBS = 
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

LIBRARY 		= $(OBJ)/libixml.a $(OBJ)/libFLAC.a  $(OBJ)/libupnp.a  $(OBJ)/libthreadutil.a $(OBJ)/libshine.a
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
