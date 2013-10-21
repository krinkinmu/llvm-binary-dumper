CXX=g++

CFLAGS=-c -Wall -Werror -pedantic -std=c++11
LDFLAGS=

LLVM_CFLAGS=$(shell llvm-config-3.4 --cxxflags)
LLVM_LDFLAGS=$(shell llvm-config-3.4 --ldflags)

LIBS=$(shell llvm-config-3.4 --libs all-targets Core Object)

default: dumper

dumper: main.o
	$(CXX) main.o $(LDFLAGS) $(LIBS) $(LLVM_LDFLAGS) -o dumper

main.o: main.cpp
	$(CXX) $(CFLAGS) $(LLVM_CFLAGS) main.cpp -o main.o

clean:
	rm -rf *.o *.swp *.swo *~ dumper

.PHONY: clean
