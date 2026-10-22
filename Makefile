# GLMetal and its test suite. Everything is built universal (x86_64 for
# Rosetta processes such as the 32-bit loader, arm64 for native games).
#
#   make -C native/glmetal glcompare     build the comparison suite
#   make -C native/glmetal check         run it: Apple's GL vs GLMetal

SDK := $(shell xcrun --sdk macosx --show-sdk-path)
ARCHS ?= -arch x86_64 -arch arm64
CC := xcrun clang
CFLAGS := $(ARCHS) -isysroot $(SDK) -mmacosx-version-min=11.0 -O2 -g -Wall -Wextra \
	-Wno-unused-parameter -DGL_SILENCE_DEPRECATION
OUT := build

GLC := tests/glcompare
GLC_CASES := $(wildcard $(GLC)/cases_*.c)

APPLE_DATA := data
GEN := $(OUT)/gen
SRC_C := $(wildcard src/*.c)
SRC_M := $(wildcard src/*.m)
SRC_CXX := $(wildcard src/*.cpp)
DEPS := third_party/install
DEP_LIBS := $(addprefix $(DEPS)/lib/lib,glslang.a MachineIndependent.a GenericCodeGen.a OSDependent.a SPIRV.a \
	glslang-default-resource-limits.a spirv-cross-msl.a spirv-cross-glsl.a spirv-cross-core.a)
# glslang / SPIRV-Cross stay private: their symbols must not clash with an
# application's own copies.
DEP_LINK := $(foreach lib,$(DEP_LIBS),-Wl,-load_hidden,$(lib))
CXX := xcrun clang++
# glslang's source tree (tools/build_deps.sh) for its AST headers.
GLSLANG_SRC := third_party/glslang
CXXFLAGS := $(ARCHS) -isysroot $(SDK) -mmacosx-version-min=11.0 -O2 -g -std=c++17 -fvisibility=hidden \
	-Wall -Wno-unused-parameter -I$(DEPS)/include -I$(GLSLANG_SRC) -Isrc
GENERATED := $(GEN)/glm_core_enums.h $(GEN)/glm_apple_profile.h $(GEN)/glm_apple_gets.h $(GEN)/attrib_setters.c $(GEN)/stubs.c \
	$(GEN)/glm_impl_rename.h $(GEN)/marshal.c
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -Isrc -I$(GEN) -Wno-unused-function -Wno-sign-compare \
	-Wno-missing-field-initializers

.PHONY: all lib inject glcompare check check-reference clean
all: lib inject glcompare
lib: $(OUT)/libGLMetal.dylib $(OUT)/glmetal-compiler

$(OUT)/client_configuration: tests/probes/client_configuration.c src/client.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: test-client-configuration
test-client-configuration: lib $(OUT)/client_configuration
	arch -x86_64 $(OUT)/client_configuration $(OUT)/libGLMetal.dylib
	arch -arm64 $(OUT)/client_configuration $(OUT)/libGLMetal.dylib
inject: $(OUT)/libGLMetalInject.dylib

# The injector: interposes Apple's OpenGL.framework onto GLMetal in an
# unmodified process (see src/inject/inject.c).
$(GEN)/inject_tables.c $(GEN)/inject_trampolines.S: tools/gen_inject.py $(APPLE_DATA)/apple-gl-exports.txt \
		$(APPLE_DATA)/apple-cgl-exports.txt
	python3 tools/gen_inject.py $(GEN) $(APPLE_DATA)/apple-gl-exports.txt $(APPLE_DATA)/apple-cgl-exports.txt

$(OUT)/libGLMetalInject.dylib: src/inject/inject.c $(GEN)/inject_tables.c $(GEN)/inject_trampolines.S
	$(CC) $(CFLAGS) -dynamiclib $^ -install_name @rpath/libGLMetalInject.dylib -framework OpenGL \
		-framework CoreFoundation -framework AppKit -o $@

$(GEN)/glm_core_enums.h: tools/gen_core_enums.py
	@mkdir -p $(GEN)
	python3 $< $(SDK) $@
$(GEN)/glm_apple_gets.h: tools/gen_get_reference.py $(APPLE_DATA)/apple-get-reference.txt
	@mkdir -p $(GEN)
	python3 $^ $@
$(GEN)/glm_apple_profile.h: tools/gen_profile.py $(APPLE_DATA)/apple-gl-reference.txt
	@mkdir -p $(GEN)
	python3 $^ $@
$(GEN)/attrib_setters.c: tools/gen_attribs.py
	@mkdir -p $(GEN)
	python3 $< $@
# The threaded entry points (tools/gen_marshal.py): marshal.c defines the
# exported gl* functions; the implementation is compiled with
# glm_impl_rename.h so its definitions become glm_impl_*.
$(GEN)/stubs.c $(GEN)/glm_impl_rename.h $(GEN)/marshal.c: tools/gen_marshal.py $(APPLE_DATA)/apple-gl-exports.txt \
		$(APPLE_DATA)/apple-cgl-exports.txt $(APPLE_DATA)/apple-profile-rejections.txt
	@mkdir -p $(GEN)
	python3 tools/gen_marshal.py $(SDK) $(GEN) $(APPLE_DATA)/apple-gl-exports.txt $(APPLE_DATA)/apple-cgl-exports.txt

# glslang and SPIRV-Cross, universal and static: tools/build_deps.sh.
$(DEPS)/lib/libglslang.a:
	tools/build_deps.sh

# The shader compiler as a native helper (src/compile_remote.cpp starts it
# from processes running under Rosetta): arm64 only.
$(OUT)/glmetal-compiler: tools/glmetal_compiler.cpp src/shader_compiler.cpp src/compile_cache.cpp src/compile_remote.cpp \
		src/shader_compiler.h $(GEN)/compiler_stamp.h $(DEPS)/lib/libglslang.a
	xcrun clang++ -arch arm64 -isysroot $(SDK) -mmacosx-version-min=11.0 -O2 -std=c++17 -I$(DEPS)/include -I$(GLSLANG_SRC) -Isrc -I$(GEN) \
		tools/glmetal_compiler.cpp src/shader_compiler.cpp src/compile_cache.cpp src/compile_remote.cpp $(DEP_LIBS) -o $@

# Serial offline request replay uses the same compiler and persistent cache.
$(OUT)/glmetal-precompile: tools/glmetal_precompile.mm $(OUT)/obj/shader_compiler.o $(OUT)/obj/compile_cache.o $(OUT)/obj/compile_remote.o
	xcrun clang++ -arch arm64 -isysroot $(SDK) -mmacosx-version-min=11.0 -O2 -std=c++17 -fobjc-arc -I$(DEPS)/include -Isrc -I$(GEN) \
		tools/glmetal_precompile.mm $(OUT)/obj/shader_compiler.o $(OUT)/obj/compile_cache.o $(OUT)/obj/compile_remote.o $(DEP_LIBS) -framework Foundation -framework Metal -o $@

# The shader compiler's identity for the disk caches: a hash of its source
# (and glslang/SPIRV-Cross), so rebuilds that do not change it keep caches.
COMPILER_INPUTS := src/shader_compiler.cpp src/shader_compiler.h src/shader_texture_bias.h src/shader_fp64.h src/shader_fp64_arithmetic.h src/shader_fp64_matrix.h src/shader_cube_shadow.h src/shader_clip.h src/compile_cache.cpp $(DEPS)/lib/libglslang.a
$(GEN)/compiler_stamp.h: $(COMPILER_INPUTS) tools/compiler_stamp.py
	@mkdir -p $(GEN)
	python3 tools/compiler_stamp.py $(COMPILER_INPUTS) > $@

$(OUT)/obj/%.o: src/%.cpp src/shader_compiler.h $(DEPS)/lib/libglslang.a $(GEN)/compiler_stamp.h
	@mkdir -p $(OUT)/obj
	$(CXX) $(CXXFLAGS) -I$(GEN) -c $< -o $@

$(OUT)/obj/shader_depth.o: src/shader_depth.h

# AppKit subclasses that must skip AppKit's -dealloc: manual retain/release.
SRC_MRC := $(wildcard src/mrc/*.m)
$(OUT)/obj/mrc_%.o: src/mrc/%.m $(GEN)/glm_impl_rename.h
	@mkdir -p $(OUT)/obj
	$(CC) $(LIB_CFLAGS) -include $(GEN)/glm_impl_rename.h -fno-objc-arc -c $< -o $@

LIB_OBJS := $(SRC_CXX:src/%.cpp=$(OUT)/obj/%.o) $(SRC_MRC:src/mrc/%.m=$(OUT)/obj/mrc_%.o)

# Compiled without the rename: the exported entry points.
$(OUT)/obj/marshal.o: $(GEN)/marshal.c src/marshal_support.h src/thread.h
	@mkdir -p $(OUT)/obj
	$(CC) $(LIB_CFLAGS) -Wno-unused-parameter -c $< -o $@
$(OUT)/obj/marshal_custom.o: src/marshal/marshal_custom.c src/marshal_support.h src/thread.h $(wildcard src/*.h) $(GENERATED)
	@mkdir -p $(OUT)/obj
	$(CC) $(LIB_CFLAGS) -c $< -o $@

LIB_OBJS += $(OUT)/obj/marshal.o $(OUT)/obj/marshal_custom.o

$(OUT)/libGLMetal.dylib: $(SRC_C) $(SRC_M) $(wildcard src/*.h) $(GENERATED) $(LIB_OBJS)
	$(CC) $(LIB_CFLAGS) -include $(GEN)/glm_impl_rename.h -dynamiclib $(SRC_C) $(GEN)/attrib_setters.c $(GEN)/stubs.c \
		$(SRC_M) -fobjc-arc $(LIB_OBJS) $(DEP_LINK) -lc++ \
		-install_name @rpath/libGLMetal.dylib -framework Metal -framework Foundation -framework QuartzCore \
		-framework CoreGraphics -framework AppKit -o $@

glcompare: $(OUT)/glcompare $(OUT)/libglcases.dylib

.PHONY: vertex-layout-bench
vertex-layout-bench: $(OUT)/glcompare $(OUT)/libvertex_layout_bench.dylib

$(OUT)/libvertex_layout_bench.dylib: tests/probes/vertex_layout_bench.c $(GLC)/glc_util.c $(GLC)/glcompare.h $(OUT)/gen/glc_gl_legacy.h
	$(CC) $(CFLAGS) -I$(GLC) -I$(OUT)/gen -dynamiclib tests/probes/vertex_layout_bench.c $(GLC)/glc_util.c \
		-Wl,-undefined,dynamic_lookup -o $@

.PHONY: sampling-bindings-bench
sampling-bindings-bench: $(OUT)/glcompare $(OUT)/libsampling_bindings_bench.dylib

$(OUT)/libsampling_bindings_bench.dylib: tests/probes/sampling_bindings_bench.c $(GLC)/glc_util.c $(GLC)/glcompare.h $(OUT)/gen/glc_gl_core.h
	$(CC) $(CFLAGS) -I$(GLC) -I$(OUT)/gen -dynamiclib tests/probes/sampling_bindings_bench.c $(GLC)/glc_util.c \
		-Wl,-undefined,dynamic_lookup -o $@

.PHONY: compile-budget-probe
compile-budget-probe: $(OUT)/glm_compile_budget

.PHONY: pipeline-compile-probes test-pipeline-prewarm
pipeline-compile-probes: $(OUT)/pipeline_compile $(OUT)/pipeline_prewarm_cpu

$(OUT)/pipeline_compile: tests/probes/pipeline_compile.m src/pipeline_prewarm.m src/pipeline_prewarm.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -fobjc-arc $< src/pipeline_prewarm.m -framework Metal -framework Foundation -o $@

$(OUT)/pipeline_prewarm_cpu: tests/probes/pipeline_prewarm_cpu.m src/pipeline_prewarm.m src/pipeline_prewarm.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -fobjc-arc $< -framework Metal -framework Foundation -o $@

test-pipeline-prewarm: $(OUT)/pipeline_prewarm_cpu
	arch -x86_64 $(OUT)/pipeline_prewarm_cpu
	arch -arm64 $(OUT)/pipeline_prewarm_cpu

$(OUT)/glm_compile_budget: tests/window/glm_compile_budget.m
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Wno-deprecated-declarations $< -framework Cocoa -framework OpenGL -o $@

$(OUT)/glcompare: $(GLC)/runner.c $(GLC)/glcompare.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -framework ApplicationServices -framework ImageIO \
		-framework CoreFoundation -o $@

$(OUT)/gen/glc_gl_legacy.h: $(GLC)/gen_gl_header.py
	python3 $< $(SDK) $(OUT)/gen

# GL calls go through glc_resolve to the implementation the runner loaded;
# only the runner's own symbols (glc_fail, glc_lookup...) bind dynamically.
$(OUT)/libglcases.dylib: $(GLC_CASES) $(GLC)/glc_util.c $(GLC)/glcompare.h $(OUT)/gen/glc_gl_legacy.h tests/probes/border_reconstruction.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(GLC) -I$(OUT)/gen -dynamiclib $(GLC_CASES) $(GLC)/glc_util.c \
		-Wl,-undefined,dynamic_lookup -o $@

# Default-framebuffer aux buffers: validation must print what Apple's GL
# does; the GLMetal run also renders into a windowless view.
$(OUT)/aux_buffers: tests/probes/aux_buffers.m
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w -fobjc-arc -framework Cocoa $< -o $@
.PHONY: test-aux-buffers
test-aux-buffers: lib $(OUT)/aux_buffers
	@$(OUT)/aux_buffers > $(OUT)/aux-buffers-apple.txt
	@$(OUT)/aux_buffers $(OUT)/libGLMetal.dylib > $(OUT)/aux-buffers-glmetal.txt 2>/dev/null || \
		{ grep WRONG $(OUT)/aux-buffers-glmetal.txt; echo "aux buffers: GLMetal rendering failed"; exit 1; }
	@GLMETAL_THREAD_ALL=1 $(OUT)/aux_buffers $(OUT)/libGLMetal.dylib > /dev/null 2>&1 || \
		{ echo "aux buffers: threaded GLMetal rendering failed"; exit 1; }
	@grep -v '^glmetal:' $(OUT)/aux-buffers-glmetal.txt | diff $(OUT)/aux-buffers-apple.txt - && \
		echo "aux buffers: matches Apple, rendering ok"

# Window-system framebuffer (front/back/aux buffers, swaps, colour
# broadcast, accumulation buffer) on Apple's GL and on GLMetal, diffed. The
# probe links the headless shim: its window is transparent, click-through,
# behind everything, and the app never activates.
SHIM := $(OUT)/external/libheadless_shim.dylib
$(OUT)/default_framebuffer: tests/probes/default_framebuffer.m
	@test -f $(SHIM) || { echo "build the headless shim first: tests/external/piglit.sh build"; exit 1; }
	$(CC) -arch arm64 -w -fobjc-arc -framework Cocoa -framework OpenGL -Wl,-needed_library,$(SHIM) $< -o $@
.PHONY: test-default-framebuffer
test-default-framebuffer: lib inject $(OUT)/default_framebuffer
	@otool -L $(OUT)/default_framebuffer | grep -qF $(SHIM) || { echo "refusing: probe does not link the headless shim"; exit 1; }
	@$(OUT)/default_framebuffer > $(OUT)/default-framebuffer-apple.txt
	@DYLD_INSERT_LIBRARIES=$(OUT)/libGLMetalInject.dylib $(OUT)/default_framebuffer > $(OUT)/default-framebuffer-glmetal.txt
	@diff $(OUT)/default-framebuffer-apple.txt $(OUT)/default-framebuffer-glmetal.txt > $(OUT)/default-framebuffer.diff && \
		echo "default framebuffer: GLMetal matches Apple ($$(wc -l < $(OUT)/default-framebuffer-apple.txt) lines)" || \
		{ echo "default framebuffer: GLMetal differs from Apple ($(OUT)/default-framebuffer.diff)"; exit 1; }

# ARB program queries (limits and per-program counters of the programs in
# tests/probes/arb_programs.txt) and load errors against Apple's answers.
$(OUT)/arb_program_queries: tests/probes/arb_program_queries.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
$(OUT)/arb_program_errors: tests/probes/arb_program_errors.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-arb-queries
test-arb-queries: lib $(OUT)/arb_program_queries $(OUT)/arb_program_errors
	@$(OUT)/arb_program_errors > $(OUT)/arb-errors-apple.txt
	@$(OUT)/arb_program_errors $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/arb-errors-apple.txt - && \
		echo "ARB program errors: match Apple" 
	@$(OUT)/arb_program_queries < tests/probes/arb_programs.txt > $(OUT)/arb-queries-apple.txt
	@$(OUT)/arb_program_queries $(OUT)/libGLMetal.dylib < tests/probes/arb_programs.txt > $(OUT)/arb-queries-glmetal.txt 2>/dev/null
	@GLMETAL_THREAD_ALL=1 $(OUT)/arb_program_queries $(OUT)/libGLMetal.dylib < tests/probes/arb_programs.txt \
		> $(OUT)/arb-queries-threaded.txt 2>/dev/null
	@diff $(OUT)/arb-queries-apple.txt $(OUT)/arb-queries-glmetal.txt && \
		diff $(OUT)/arb-queries-apple.txt $(OUT)/arb-queries-threaded.txt && \
		echo "ARB program queries: direct/threaded match Apple ($$(wc -l < $(OUT)/arb-queries-apple.txt) lines)"

# ARB programs with the NV options (tests/probes/nv_programs.txt): load
# errors, computed values and counters against Apple's, less the documented
# lines where Apple contradicts the extensions; GLMetal's TXD against
# Apple's GLSL texture*GradARB (Apple runs TXD as a MOV).
$(OUT)/nv_programs: tests/probes/nv_programs.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-nv-programs
test-nv-programs: lib $(OUT)/nv_programs $(OUT)/arb_program_queries
	@$(OUT)/nv_programs < tests/probes/nv_programs.txt > $(OUT)/nv-programs-apple.txt 2>/dev/null
	@$(OUT)/nv_programs $(OUT)/libGLMetal.dylib < tests/probes/nv_programs.txt > $(OUT)/nv-programs-direct.txt 2>/dev/null
	@GLMETAL_THREAD_ALL=1 $(OUT)/nv_programs $(OUT)/libGLMetal.dylib < tests/probes/nv_programs.txt \
		> $(OUT)/nv-programs-threaded.txt 2>/dev/null
	@$(OUT)/nv_programs < tests/probes/nv_programs_grad.txt > $(OUT)/nv-programs-grad-apple.txt 2>/dev/null
	@$(OUT)/arb_program_queries < tests/probes/nv_programs.txt > $(OUT)/nv-queries-apple.txt
	@$(OUT)/arb_program_queries $(OUT)/libGLMetal.dylib < tests/probes/nv_programs.txt > $(OUT)/nv-queries-glmetal.txt 2>/dev/null
	@python3 tests/probes/quirkdiff.py $(OUT)/nv-programs-apple.txt $(OUT)/nv-programs-direct.txt tests/probes/nv_programs_quirks.txt && \
		python3 tests/probes/quirkdiff.py $(OUT)/nv-programs-apple.txt $(OUT)/nv-programs-threaded.txt tests/probes/nv_programs_quirks.txt && \
		python3 tests/probes/quirkdiff.py $(OUT)/nv-queries-apple.txt $(OUT)/nv-queries-glmetal.txt tests/probes/nv_queries_quirks.txt && \
		python3 tests/probes/nv_twins.py $(OUT)/nv-programs-grad-apple.txt $(OUT)/nv-programs-direct.txt && \
		echo "NV programs: direct/threaded/counters match Apple ($$(grep -c '^%%' tests/probes/nv_programs.txt) programs, $$(grep -c '^apple:' tests/probes/nv_programs_quirks.txt) documented quirks)"

# Small legacy entry points (colour index, texture residency, vertex blend
# state, unadvertised extensions) against Apple's answers.
$(OUT)/legacy_misc: tests/probes/legacy_misc.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-legacy-misc
test-legacy-misc: lib $(OUT)/legacy_misc
	@$(OUT)/legacy_misc > $(OUT)/legacy-misc-apple.txt
	@$(OUT)/legacy_misc $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/legacy-misc-apple.txt - && \
		GLMETAL_THREAD_ALL=1 $(OUT)/legacy_misc $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/legacy-misc-apple.txt - && \
		echo "legacy entry points: direct/threaded match Apple ($$(wc -l < $(OUT)/legacy-misc-apple.txt) lines)"

# The imaging subset against Apple's answers, less the documented lines
# where Apple contradicts the extensions (tests/probes/imaging_quirks.txt).
$(OUT)/imaging: tests/probes/imaging.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-imaging
test-imaging: lib $(OUT)/imaging
	@$(OUT)/imaging > $(OUT)/imaging-apple.txt
	@$(OUT)/imaging $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/imaging-direct.txt
	@GLMETAL_THREAD_ALL=1 $(OUT)/imaging $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/imaging-threaded.txt
	@python3 tests/probes/quirkdiff.py $(OUT)/imaging-apple.txt $(OUT)/imaging-direct.txt tests/probes/imaging_quirks.txt && \
		python3 tests/probes/quirkdiff.py $(OUT)/imaging-apple.txt $(OUT)/imaging-threaded.txt tests/probes/imaging_quirks.txt && \
		echo "imaging: direct/threaded match Apple ($$(wc -l < $(OUT)/imaging-apple.txt) lines, $$(grep -c '^apple:' tests/probes/imaging_quirks.txt) documented quirks)"

# Attribute evaluators (APPLE_vertex_program_evaluators) against Apple's answers.
$(OUT)/attrib_evaluators: tests/probes/attrib_evaluators.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-attrib-evaluators
test-attrib-evaluators: lib $(OUT)/attrib_evaluators
	@$(OUT)/attrib_evaluators > $(OUT)/attrib-evaluators-apple.txt
	@$(OUT)/attrib_evaluators $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/attrib-evaluators-direct.txt
	@GLMETAL_THREAD_ALL=1 $(OUT)/attrib_evaluators $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/attrib-evaluators-threaded.txt
	@python3 tests/probes/quirkdiff.py $(OUT)/attrib-evaluators-apple.txt $(OUT)/attrib-evaluators-direct.txt tests/probes/attrib_evaluators_quirks.txt && \
		python3 tests/probes/quirkdiff.py $(OUT)/attrib-evaluators-apple.txt $(OUT)/attrib-evaluators-threaded.txt tests/probes/attrib_evaluators_quirks.txt && \
		echo "attribute evaluators: direct/threaded match Apple ($$(wc -l < $(OUT)/attrib-evaluators-apple.txt) lines, $$(grep -c '^apple:' tests/probes/attrib_evaluators_quirks.txt) documented quirks)"

# Vertex blending and per-vertex point sizes against Apple's answers.
$(OUT)/vertex_blend: tests/probes/vertex_blend.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-vertex-blend
test-vertex-blend: lib $(OUT)/vertex_blend
	@$(OUT)/vertex_blend > $(OUT)/vertex-blend-apple.txt
	@$(OUT)/vertex_blend $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/vertex-blend-direct.txt
	@GLMETAL_THREAD_ALL=1 $(OUT)/vertex_blend $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/vertex-blend-threaded.txt
	@python3 tests/probes/numdiff.py $(OUT)/vertex-blend-apple.txt $(OUT)/vertex-blend-direct.txt && \
		python3 tests/probes/numdiff.py $(OUT)/vertex-blend-apple.txt $(OUT)/vertex-blend-threaded.txt && \
		echo "vertex blend: direct/threaded match Apple ($$(wc -l < $(OUT)/vertex-blend-apple.txt) lines)"

# Bindable uniforms (EXT_bindable_uniform) against Apple's answers.
$(OUT)/bindable_uniform: tests/probes/bindable_uniform.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-bindable-uniform
test-bindable-uniform: lib $(OUT)/bindable_uniform
	@$(OUT)/bindable_uniform > $(OUT)/bindable-uniform-apple.txt
	@$(OUT)/bindable_uniform $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/bindable-uniform-direct.txt
	@GLMETAL_THREAD_ALL=1 $(OUT)/bindable_uniform $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/bindable-uniform-threaded.txt
	@python3 tests/probes/quirkdiff.py $(OUT)/bindable-uniform-apple.txt $(OUT)/bindable-uniform-direct.txt tests/probes/bindable_uniform_quirks.txt && \
		python3 tests/probes/quirkdiff.py $(OUT)/bindable-uniform-apple.txt $(OUT)/bindable-uniform-threaded.txt tests/probes/bindable_uniform_quirks.txt && \
		echo "bindable uniforms: direct/threaded match Apple ($$(wc -l < $(OUT)/bindable-uniform-apple.txt) lines, $$(grep -c '^apple:' tests/probes/bindable_uniform_quirks.txt) documented quirk)"

# Element arrays (APPLE_element_array) against Apple's answers.
$(OUT)/element_array: tests/probes/element_array.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-element-array
test-element-array: lib $(OUT)/element_array
	@$(OUT)/element_array > $(OUT)/element-array-apple.txt
	@$(OUT)/element_array $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/element-array-apple.txt - && \
		GLMETAL_THREAD_ALL=1 $(OUT)/element_array $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/element-array-apple.txt - && \
		echo "element arrays: direct/threaded match Apple ($$(wc -l < $(OUT)/element-array-apple.txt) lines)"

# Legacy GLSL linking and active uniform reflection against Apple's answers.
$(OUT)/legacy_reflection: tests/probes/legacy_reflection.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-legacy-reflection
test-legacy-reflection: lib $(OUT)/legacy_reflection
	@$(OUT)/legacy_reflection > $(OUT)/legacy-reflection-apple.txt
	@GLMETAL_NO_SHADER_CACHE=1 $(OUT)/legacy_reflection $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/legacy-reflection-apple.txt - && \
		GLMETAL_NO_SHADER_CACHE=1 GLMETAL_THREAD_ALL=1 $(OUT)/legacy_reflection $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/legacy-reflection-apple.txt - && \
		echo "legacy reflection: direct/threaded match Apple ($$(wc -l < $(OUT)/legacy-reflection-apple.txt) lines)"

# Evaluators against Apple's answers.
$(OUT)/evaluators: tests/probes/evaluators.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-evaluators
test-evaluators: lib $(OUT)/evaluators
	@$(OUT)/evaluators > $(OUT)/evaluators-apple.txt
	@$(OUT)/evaluators $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/evaluators-direct.txt
	@GLMETAL_THREAD_ALL=1 $(OUT)/evaluators $(OUT)/libGLMetal.dylib 2>/dev/null > $(OUT)/evaluators-threaded.txt
	@python3 tests/probes/numdiff.py $(OUT)/evaluators-apple.txt $(OUT)/evaluators-direct.txt && \
		python3 tests/probes/numdiff.py $(OUT)/evaluators-apple.txt $(OUT)/evaluators-threaded.txt && \
		echo "evaluators: direct/threaded match Apple ($$(wc -l < $(OUT)/evaluators-apple.txt) lines)"

# Selection and feedback render modes against Apple's answers.
$(OUT)/render_modes: tests/probes/render_modes.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
.PHONY: test-render-modes
test-render-modes: lib $(OUT)/render_modes
	@$(OUT)/render_modes > $(OUT)/render-modes-apple.txt
	@$(OUT)/render_modes $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/render-modes-apple.txt - && \
		GLMETAL_THREAD_ALL=1 $(OUT)/render_modes $(OUT)/libGLMetal.dylib 2>/dev/null | diff $(OUT)/render-modes-apple.txt - && \
		echo "render modes: direct/threaded match Apple ($$(wc -l < $(OUT)/render-modes-apple.txt) lines)"

check: glcompare check-reference
	python3 $(GLC)/compare.py

# GLMetal against Apple's recorded API behaviour (data/apple-get-reference.txt:
# getters, capabilities, renderbuffer and texture formats), direct and on the
# command stream. No windows: surfaceless contexts only.
$(OUT)/apple_get_sweep: tools/apple_get_sweep.c
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w $< -o $@
check-reference: lib $(OUT)/apple_get_sweep
	@grep -v '^#' $(APPLE_DATA)/apple-get-reference.txt > $(OUT)/apple-get-reference.expected
	@$(OUT)/apple_get_sweep $(OUT)/libGLMetal.dylib 2>/dev/null | grep -v '^#' | \
		diff $(OUT)/apple-get-reference.expected - > $(OUT)/reference-direct.diff && echo "reference: direct matches Apple" || \
		{ echo "reference: direct differs from Apple ($(OUT)/reference-direct.diff)"; exit 1; }
	@GLMETAL_THREAD_ALL=1 $(OUT)/apple_get_sweep $(OUT)/libGLMetal.dylib 2>/dev/null | grep -v '^#' | \
		diff $(OUT)/apple-get-reference.expected - > $(OUT)/reference-threaded.diff && echo "reference: threaded matches Apple" || \
		{ echo "reference: threaded differs from Apple ($(OUT)/reference-threaded.diff)"; exit 1; }

# Which enums each setter, getter and command accepts (tools/apple_enum_sweep.c)
# against Apple's (data/apple-enum-reference.txt), less the documented
# differences (tests/probes/enum_sweep_quirks.txt), direct and threaded.
$(GEN)/sweep_enums.h: tools/gen_sweep_enums.py
	@mkdir -p $(GEN)
	python3 $< $(SDK)/System/Library/Frameworks/OpenGL.framework/Headers $@
$(OUT)/apple_enum_sweep: tools/apple_enum_sweep.c $(GEN)/sweep_enums.h
	@mkdir -p $(OUT)
	$(CC) -arch arm64 -w -I$(GEN) $< -o $@
.PHONY: check-enums
check-enums: lib $(OUT)/apple_enum_sweep
	@grep -v '^#' $(APPLE_DATA)/apple-enum-reference.txt > $(OUT)/apple-enum-reference.expected
	@$(OUT)/apple_enum_sweep $(OUT)/libGLMetal.dylib > $(OUT)/enums-direct.txt 2>/dev/null
	@GLMETAL_THREAD_ALL=1 $(OUT)/apple_enum_sweep $(OUT)/libGLMetal.dylib > $(OUT)/enums-threaded.txt 2>/dev/null
	@python3 tests/probes/keydiff.py $(OUT)/apple-enum-reference.expected $(OUT)/enums-direct.txt tests/probes/enum_sweep_quirks.txt && \
		python3 tests/probes/keydiff.py $(OUT)/apple-enum-reference.expected $(OUT)/enums-threaded.txt tests/probes/enum_sweep_quirks.txt && \
		echo "enums: direct/threaded match Apple ($$(wc -l < $(OUT)/apple-enum-reference.expected) lines, $$(grep -c '^apple:' tests/probes/enum_sweep_quirks.txt) documented differences)"

clean:
	rm -rf $(OUT)

# CPU-only sampler metadata checks. An optional request directory benchmarks
# existing .prog reflection without compiling shaders or invoking Metal.
$(OUT)/sampler_plan_cpu: tests/probes/sampler_plan_cpu.cpp src/sampler_plan.h $(OUT)/obj/shader_compiler.o $(OUT)/obj/compile_cache.o $(OUT)/obj/compile_remote.o
	$(CXX) $(CXXFLAGS) tests/probes/sampler_plan_cpu.cpp $(OUT)/obj/shader_compiler.o $(OUT)/obj/compile_cache.o $(OUT)/obj/compile_remote.o $(DEP_LIBS) -o $@

.PHONY: test-sampler-plan
test-sampler-plan: $(OUT)/sampler_plan_cpu
	arch -x86_64 $(OUT)/sampler_plan_cpu
	arch -arm64 $(OUT)/sampler_plan_cpu
