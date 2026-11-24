CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2

ifeq ($(shell uname -s),Darwin)
LIB_EXT = dylib
LIB_FLAGS = -dynamiclib -Wl,-install_name,@rpath/$@
RPATH = -Wl,-rpath,@loader_path
DL_LIBS =
else
LIB_EXT = so
LIB_FLAGS = -shared
RPATH = -Wl,-rpath,'$$ORIGIN'
DL_LIBS = -ldl
endif

LIBRARIES = libimpl1.$(LIB_EXT) libimpl2.$(LIB_EXT)

.PHONY: all lab5 clean

all: lab5

lab5: $(LIBRARIES) program1 program2

libimpl1.$(LIB_EXT): impl1.cpp contracts.hpp
	$(CXX) $(CXXFLAGS) -fPIC $(LIB_FLAGS) $< -o $@

libimpl2.$(LIB_EXT): impl2.cpp contracts.hpp
	$(CXX) $(CXXFLAGS) -fPIC $(LIB_FLAGS) $< -o $@

program1: program1.cpp commands.hpp contracts.hpp libimpl1.$(LIB_EXT)
	$(CXX) $(CXXFLAGS) $< -L. -limpl1 $(RPATH) -o $@

program2: program2.cpp commands.hpp contracts.hpp | $(LIBRARIES)
	$(CXX) $(CXXFLAGS) -DLIB_EXT='"$(LIB_EXT)"' $< $(DL_LIBS) -o $@

clean:
	rm -f program1 program2 libimpl1.so libimpl2.so libimpl1.dylib libimpl2.dylib
