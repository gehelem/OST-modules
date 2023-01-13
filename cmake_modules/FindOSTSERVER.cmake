include(FindPackageHandleStandardArgs)

SET(OST_NAMES ${OST_NAMES}  ostindimodule ostbasemodule)
find_library(OST_LIBRARY_BASE NAMES ostbasemodule)
find_library(OST_LIBRARY_INDI NAMES ostindimodule)

INCLUDE(FindPackageHandleStandardArgs)
#find_package_handle_standard_args(OST REQUIRED_VARS OST_LIBRARY OST_INCLUDE_DIR)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OST DEFAULT_MSG  OST_LIBRARY_INDI OST_LIBRARY_BASE OST_INCLUDE_DIR)
find_path(OST_INCLUDE_DIR indimodule.h basemodule.h)



if (OST_FOUND)
    mark_as_advanced(OST_INCLUDE_DIR)
    mark_as_advanced(OST_LIBRARY_BASE)
    mark_as_advanced(OST_LIBRARY_INDI)
    SET(OST_LIBRARIES OST_LIBRARY_INDI OST_LIBRARY_BASE )
else (OST_FOUND)
    message(FATAL_ERROR "OST is not found. Please install it first.")
endif(OST_FOUND)
