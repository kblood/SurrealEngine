if(NOT DEFINED GENERATED_JAVASCRIPT OR GENERATED_JAVASCRIPT STREQUAL "")
	message(FATAL_ERROR "GENERATED_JAVASCRIPT is required")
endif()

if(NOT EXISTS "${GENERATED_JAVASCRIPT}")
	message(FATAL_ERROR "Generated Emscripten JavaScript does not exist: ${GENERATED_JAVASCRIPT}")
endif()

file(READ "${GENERATED_JAVASCRIPT}" generated_javascript)

# This is intentionally a generated-output gate rather than a source-only
# assertion: it catches an Emscripten library ordering change that silently
# replaces the project override with the built-in TextDecoder initializer.
if(NOT generated_javascript MATCHES "var[ \t\r\n]+UTF8Decoder[ \t\r\n]*=[ \t\r\n]*(null|false)")
	message(FATAL_ERROR
		"The shipping Emscripten output did not disable its internal UTF8Decoder")
endif()

if(generated_javascript MATCHES
		"var[ \t\r\n]+UTF8Decoder[ \t\r\n]*=[^;\r\n]*new[ \t\r\n]+TextDecoder")
	message(FATAL_ERROR
		"The shipping Emscripten output reinstated TextDecoder for Wasm-backed strings")
endif()

# TEXTDECODER=1 must retain the scalar branch that the null decoder selects.
if(NOT generated_javascript MATCHES
		"while[ \t\r\n]*\\([ \t\r\n]*idx[ \t\r\n]*<[ \t\r\n]*endPtr[ \t\r\n]*\\)")
	message(FATAL_ERROR
		"The shipping Emscripten output does not contain the scalar UTF-8 fallback")
endif()

# ALLOW_MEMORY_GROWTH=1 emits the WebAssembly.Memory growth path. Fixed-memory
# builds omit this call, so this also guards against restoring the 256 MiB cap.
string(FIND "${generated_javascript}" "wasmMemory.grow(" memory_growth_position)
if(memory_growth_position EQUAL -1)
	message(FATAL_ERROR
		"The shipping Emscripten output does not contain WebAssembly memory growth")
endif()

message(STATUS
	"Verified growable WebAssembly memory with Emscripten scalar UTF-8 decoding")
