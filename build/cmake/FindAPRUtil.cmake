find_path(APRUTIL_INCLUDE_DIR
  NAMES apu.h
  PATH_SUFFIXES
    include
    include/apr-1 # Not yet in apr
)

find_library(APRUTIL_LIBRARY_SHARED
  NAMES libaprutil-1
  PATH_SUFFIXES lib
)

find_library(APRUTIL_LIBRARY_STATIC
  NAMES aprutil-1
  PATH_SUFFIXES lib
)

find_file(APRUTIL_DLL
  NAMES libaprutil-1.dll
  PATH_SUFFIXES bin
)

if(APRUTIL_LIBRARY_SHARED)
  set(APRUTIL_LIBRARY ${APRUTIL_LIBRARY_SHARED})
elseif(APRUTIL_LIBRARY_STATIC)
  set(APRUTIL_LIBRARY ${APRUTIL_LIBRARY_STATIC})
endif()

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  APRUtil
  REQUIRED_VARS
    APRUTIL_LIBRARY
    APRUTIL_INCLUDE_DIR
)

if(APRUtil_FOUND)
  if(NOT TARGET apr::aprutil)
    if (APRUTIL_LIBRARY_SHARED)
      add_library(apr::aprutil SHARED IMPORTED)
      target_compile_definitions(apr::aprutil INTERFACE "APU_DECLARE_IMPORT")
      set_target_properties(apr::aprutil PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${APRUTIL_INCLUDE_DIR}
        IMPORTED_LOCATION ${APRUTIL_DLL}
        IMPORTED_IMPLIB ${APRUTIL_LIBRARY}
      )
    else()
      add_library(apr::aprutil STATIC IMPORTED)
      target_compile_definitions(apr::aprutil INTERFACE "APU_DECLARE_STATIC")
      set_target_properties(apr::aprutil PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${APRUTIL_INCLUDE_DIR}
        IMPORTED_LOCATION ${APRUTIL_LIBRARY}
      )
    endif()
  endif()
endif()
