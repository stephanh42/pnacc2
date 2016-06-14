# Customize these for your installation.
CPPFLAGS=-I/opt/local/include
LDFLAGS=-L/opt/local/lib

TARGET=pnacc2

$(TARGET) : pnacc2.cc
	g++ -Wall -O2 -std=c++11 $(CPPFLAGS) $< -o $@ $(LDFLAGS) -ltbb

.PHONY: clean
clean:
	-rm $(TARGET)
