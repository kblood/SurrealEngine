if(NOT DEFINED GENERATED_JAVASCRIPT OR GENERATED_JAVASCRIPT STREQUAL "")
	message(FATAL_ERROR "GENERATED_JAVASCRIPT is required")
endif()

if(NOT EXISTS "${GENERATED_JAVASCRIPT}")
	message(FATAL_ERROR "Generated Emscripten JavaScript does not exist: ${GENERATED_JAVASCRIPT}")
endif()

file(READ "${GENERATED_JAVASCRIPT}" generated_javascript)

# Browser APIs such as TextDecoder, Web Crypto, and WebGPU reject views backed
# by resizable ArrayBuffers. GROWABLE_ARRAYBUFFERS=0 must keep Emscripten on its
# ordinary ArrayBuffer path and refresh HEAP views after WebAssembly growth.
string(FIND "${generated_javascript}" "toResizableBuffer(" resizable_buffer_position)
if(NOT resizable_buffer_position EQUAL -1)
	message(FATAL_ERROR
		"The shipping Emscripten output can expose resizable Wasm-backed views")
endif()

string(FIND "${generated_javascript}" "return wasmMemory.buffer" fixed_buffer_position)
if(fixed_buffer_position EQUAL -1)
	message(FATAL_ERROR
		"The shipping Emscripten output does not select ordinary Wasm memory buffers")
endif()

# ALLOW_MEMORY_GROWTH=1 emits the WebAssembly.Memory growth path. Fixed-memory
# builds omit this call, so this also guards against restoring the 256 MiB cap.
string(FIND "${generated_javascript}" "wasmMemory.grow(" memory_growth_position)
if(memory_growth_position EQUAL -1)
	message(FATAL_ERROR
		"The shipping Emscripten output does not contain WebAssembly memory growth")
endif()

if(NOT generated_javascript MATCHES
		"wasmMemory\\.grow\\([^)]*\\)[ \t\r\n]*;[ \t\r\n]*updateMemoryViews\\(\\)")
	message(FATAL_ERROR
		"The shipping Emscripten output does not refresh HEAP views after growth")
endif()

message(STATUS
	"Verified growable WebAssembly memory with ordinary browser-safe views")
