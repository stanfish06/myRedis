CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

all: server client

server: server.cpp utils.h containers.h
	$(CXX) $(CXXFLAGS) -o $@ server.cpp

client: client.cpp utils.h
	$(CXX) $(CXXFLAGS) -o $@ client.cpp

test: all
	./test.sh

clean:
	rm -f server client

.PHONY: all test clean
