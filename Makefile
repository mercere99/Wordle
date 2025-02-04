EMP_DIR := ../../Dropbox/Development/Empirical
OUT_DIR := http://localhost/~charles/Wordle/

#WEB_MEM := 67108864
WEB_MEM := 268435456

# Flags to use regardless of compiler
CFLAGS_all := -std=c++20 -Wall -Wextra -Wno-unused-function # -I$(EMP_DIR)/include/

# Emscripten compiler information
CXX_web := emcc
CXX_native := g++

OFLAGS_native_debug := -g -pedantic -DEMP_TRACK_MEM  -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual -Wconversion -Weffc++
OFLAGS_native_opt := -O3 -DNDEBUG

OFLAGS_web_debug := -g4 -pedantic -Wno-dollar-in-identifier-extension -Wno-gnu-zero-variadic-macro-arguments -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap']" -s TOTAL_MEMORY=$(WEB_MEM) -s ASSERTIONS=2 -s DEMANGLE_SUPPORT=1 -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual -Wconversion -Weffc++ --source-map-base $(OUT_DIR)
 # -s SAFE_HEAP=1
OFLAGS_web_opt := -Os -DNDEBUG -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap']" -s TOTAL_MEMORY=$(WEB_MEM)

CFLAGS_native_debug := $(CFLAGS_all) $(OFLAGS_native_debug)
CFLAGS_native_opt := $(CFLAGS_all) $(OFLAGS_native_opt)
CFLAGS_native := $(CFLAGS_native_opt)

CFLAGS_web_debug := $(CFLAGS_all) $(OFLAGS_web_debug) --js-library $(EMP_DIR)/include/emp/web/library_emp.js -s EXPORTED_FUNCTIONS="['_main', '_empCppCallback']" -s NO_EXIT_RUNTIME=1
CFLAGS_web_opt := $(CFLAGS_all) $(OFLAGS_web_opt) --js-library $(EMP_DIR)/include/emp/web/library_emp.js -s EXPORTED_FUNCTIONS="['_main', '_empCppCallback']" -s NO_EXIT_RUNTIME=1


JS_TARGETS := Wordle.js
CPP_TARGETS := Wordle-CommandLine

default: native

# CXX := $(CXX_native)
# CFLAGS := $(CFLAGS_native_opt)
CXX := $(CXX_web)
CFLAGS := $(CFLAGS_web_opt)

native: CXX := $(CXX_native)
native: CFLAGS := $(CFLAGS_native_opt)
native: $(CPP_TARGETS)

debug: CXX := $(CXX_native)
debug: CFLAGS := $(CFLAGS_native_debug)
debug: $(CPP_TARGETS)

web: CXX := $(CXX_web)
web: CFLAGS_web := $(CFLAGS_web_opt)
web: $(JS_TARGETS)

web-debug: CXX := $(CXX_web)
web-debug: CFLAGS := $(CFLAGS_web_debug)
web-debug: $(JS_TARGETS)

$(JS_TARGETS): %.js : %.cpp # $(WEB)/%.h
	$(CXX_web) $(CFLAGS_web) $< -o $@


$(CPP_TARGETS): % : %.cpp
	$(CXX) $(CFLAGS) $< -o $@

debug-%: $*.cpp
	$(CXX_native) $(CFLAGS_native) $< -o $@

clean:
	rm -f debug-* $(JS_TARGETS) $(CPP_TARGETS) *.js.map *.js.mem *~ *.o

# Debugging information
#print-%: ; @echo $*=$($*)
print-%: ; @echo '$(subst ','\'',$*=$($*))'
