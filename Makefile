CXX?= g++
PREFIX?=    /usr/local
DESTDIR?=   
SOURCES=    main.cpp data.cpp editor.cpp files.cpp gui.cpp linux.cpp player.cpp
TARGET=     mariopants
EXEPATH=    ${PREFIX}/bin
MANPAGE=    mariopants.1
MANPATH=    ${PREFIX}/share/man/man1

CFLAGS+=    -DUSE_INTERP_RESULT -Wno-deprecated-declarations \
            `sdl-config --cflags` `pkg-config --cflags tk`

LIBS+=      `sdl-config --libs` \
            `pkg-config --libs tk` \
            -lz -lm

OBJ=${SOURCES:%.cpp=%.o}

all: ${TARGET}

${OBJ}: %.o : %.cpp
	${CXX} -o $@ -c $< ${CFLAGS}

${TARGET}: ${OBJ}
	${CXX} -o $@ $^ ${LIBS}


clean:
	rm *.o
	rm $(TARGET)

install: ${TARGET}
	install -D $(TARGET) ${DESTDIR}$(EXEPATH)/$(TARGET)
	install -D -g 0 -o 0 -m 0664 $(MANPAGE) ${DESTDIR}$(MANPATH)/$(MANPAGE)


.PHONY: clean