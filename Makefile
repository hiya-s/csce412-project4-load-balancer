CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
TARGET := load_balancer
SOURCES := main.cpp Config.cpp IPBlocker.cpp WebServer.cpp LoadBalancer.cpp
OBJECTS := $(SOURCES:.cpp=.o)
TEST_TARGET := load_balancer_tests
TEST_SOURCES := tests.cpp Config.cpp IPBlocker.cpp WebServer.cpp LoadBalancer.cpp

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) config.txt

$(TEST_TARGET): $(TEST_SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SOURCES)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	$(RM) $(OBJECTS) $(TARGET) $(TEST_TARGET)