CXX=g++

TARGET=TunnSock

LIB=-L /usr/local/lib /usr/local/lib/libboost_system.a  /usr/local/lib/libboost_thread.a -lpthread

INCFLGS=-I./#-I/usr/local/include/boost

OBJ=TunnelSock.o\
    Process.o\
    Client.o

${TARGET}:$(OBJ)
	${CXX} -Wall -g -pipe -o $@ $(INCFLGS) $^ ${LIB}


.PHONY:clean
clean:
	-rm -rf *.o

%.o:../%.c
	gcc -Wall -g -pipe $(INCFLGS) -c -o $@ $<
%.o: %.cpp
	g++ $(CXXFLAGS) -Wall -g -Wno-deprecated -pipe $(INCFLGS) -c -o $@ $<
%.o: ../%.cpp
	gcc -Wall -g -pipe $(INCFLGS) -c -o $@ $<