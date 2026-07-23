if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED GIT_EXECUTABLE OR
		NOT DEFINED COMPILER_EXECUTABLE OR NOT BROWSER_ENTRY_POINT MATCHES "^(call-main|asyncify-opfs)$")
	message(FATAL_ERROR "Browser build provenance requires source/toolchain fields and a supported browser entry point")
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" rev-parse HEAD
	OUTPUT_VARIABLE source_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" rev-parse "HEAD^{tree}"
	OUTPUT_VARIABLE source_tree OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" status --porcelain=v1 --untracked-files=all
	OUTPUT_VARIABLE source_status OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(source_status STREQUAL "")
	set(source_dirty false)
else()
	set(source_dirty true)
endif()

execute_process(COMMAND "${COMPILER_EXECUTABLE}" --version
	OUTPUT_VARIABLE compiler_description OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "emcc[^\r\n]* ([0-9]+\\.[0-9]+\\.[0-9]+) \\(([0-9a-f]+)\\)"
	emscripten_match "${compiler_description}")
if(NOT emscripten_match)
	message(FATAL_ERROR "Could not extract the Emscripten version and revision from compiler --version")
endif()
set(emscripten_version "${CMAKE_MATCH_1}")
set(emscripten_revision "${CMAKE_MATCH_2}")

set(contents "{\n")
string(APPEND contents "  \"schema\": \"surrealengine-browser-build-provenance-v1\",\n")
string(APPEND contents "  \"sourceCommit\": \"${source_commit}\",\n")
string(APPEND contents "  \"sourceTree\": \"${source_tree}\",\n")
string(APPEND contents "  \"sourceDirty\": ${source_dirty},\n")
string(APPEND contents "  \"toolchain\": \"Emscripten\",\n")
string(APPEND contents "  \"emscriptenVersion\": \"${emscripten_version}\",\n")
string(APPEND contents "  \"emscriptenRevision\": \"${emscripten_revision}\",\n")
string(APPEND contents "  \"compilerId\": \"${COMPILER_ID}\",\n")
string(APPEND contents "  \"compilerVersion\": \"${COMPILER_VERSION}\",\n")
string(APPEND contents "  \"cmakeVersion\": \"${CMAKE_VERSION_VALUE}\",\n")
string(APPEND contents "  \"browserEntryPoint\": \"${BROWSER_ENTRY_POINT}\",\n")
string(APPEND contents "  \"surrealVideoLinkage\": \"static-wasm\"\n")
string(APPEND contents "}\n")

set(temporary "${OUTPUT_FILE}.tmp")
file(WRITE "${temporary}" "${contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${temporary}" "${OUTPUT_FILE}"
	COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE "${temporary}")
