CXXFLAGS=-std=c++11 -Wall -Wextra -Werror $(EXTRA_CXXFLAGS)
LDFLAGS=$(EXTRA_LDFLAGS)

generator: generator.cpp
		$(CXX) -o generator $(CXXFLAGS) generator.cpp $(EXTRA_LDFLAGS) -ljsoncpp -ltinyxml2
