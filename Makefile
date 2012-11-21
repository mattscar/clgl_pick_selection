PROJ=pick_sphere

CC=g++

CFLAGS=-Wall -g -DDEBUG

TINYXML_SRC = tinyxml/tinyxml.cpp tinyxml/tinystr.cpp \
tinyxml/tinyxmlerror.cpp tinyxml/tinyxmlparser.cpp

LIBS=-lglut -lOpenCL

INC_DIRS = -I$(AMDAPPSDKROOT)/include
LIB_DIRS = -L$(AMDAPPSDKROOT)/lib/x86_64

$(PROJ): $(PROJ).cpp colladainterface.cpp $(TINYXML_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(INC_DIRS) $(LIB_DIRS) $(LIBS)

.PHONY: clean

clean:
	rm $(PROJ)
