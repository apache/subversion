find_package(Python COMPONENTS Interpreter REQUIRED)

function(target_exports target_name)
  if (WIN32)
    set(def_file_path "${CMAKE_BINARY_DIR}/${target_name}.def")

    add_custom_command(
      WORKING_DIRECTORY
        "${CMAKE_SOURCE_DIR}"
      COMMAND
        "${Python_EXECUTABLE}"
      ARGS
        "build/generator/extractor.py"
        ${ARGN}
        ">${def_file_path}"
      OUTPUT
        "${def_file_path}"
      DEPENDS
        "build/generator/extractor.py"
        ${ARGN}
    )

    target_sources("${target_name}" PRIVATE "${def_file_path}")
  endif()
endfunction()
