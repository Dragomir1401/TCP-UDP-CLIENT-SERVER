SERVER_PROJECT=server
SERVER_SOURCES=server.c linked_list.c
CLIENT_PROJECT=subscriber
CLIENT_SOURCES=subscriber.c

LIBRARY=nope
INCPATHS=include
LIBPATHS=.
LDFLAGS=
CFLAGS=-g -c -Wall -Werror -Wno-error=unused-variable
CC=gcc

# Automatic generation of some important lists
SERVER_OBJECTS=$(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS=$(CLIENT_SOURCES:.c=.o)

INCFLAGS=$(foreach TMP,$(INCPATHS),-I$(TMP))
LIBFLAGS=$(foreach TMP,$(LIBPATHS),-L$(TMP))

# Set up the output file names for the different output types
SERVER_BINARY=$(SERVER_PROJECT)
CLIENT_BINARY=$(CLIENT_PROJECT)

all: $(SERVER_SOURCES) $(SERVER_BINARY) $(CLIENT_SOURCES) $(CLIENT_BINARY)

$(SERVER_BINARY): $(SERVER_OBJECTS)
	$(CC) $(LIBFLAGS) $(SERVER_OBJECTS) -o $@

$(CLIENT_BINARY): $(CLIENT_OBJECTS)
	$(CC) $(LIBFLAGS) $(CLIENT_OBJECTS) -o $@

.c.o:
	$(CC) $(INCFLAGS) $(CFLAGS) -fPIC $< -o $@

clean:
	rm -rf $(SERVER_OBJECTS) $(CLIENT_OBJECTS) server subscriber
