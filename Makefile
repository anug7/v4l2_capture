INCLUDES = `pkg-config --cflags opencv`
LIBS = `pkg-config --libs opencv` -lpthread

CXX = g++
LDFLAGS = ${LIBS}
CFLAGS = -I${INCLUDES}
CXXFLAGS =

all: lib
	
lib:
	${CXX} -c camera.cpp ${CFLAGS} ${LDFLAGS}
	ar rcs libv4l2_capture.a camera.o

clean:
	rm libv4l2_capture.a *.o
