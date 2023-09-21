cmake_minimum_required(VERSION 3.0)
PROJECT(OST-MODULES C CXX)
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-fno-sized-deallocation)
add_compile_options(-fPIC)

#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
set(THREADS_PREFER_PTHREAD_FLAG ON)

#find_package(Qt5Core)
find_package(CFITSIO REQUIRED)
find_package(INDI REQUIRED)
#find_package(INDI COMPONENTS clientqt5 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(Nova REQUIRED)
find_package(GSL REQUIRED)
#find_package(OpenCV REQUIRED )
find_package(WCSLIB REQUIRED)
find_package(QT NAMES Qt5 COMPONENTS Gui Widgets Core Concurrent Network WebSockets Sql REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Gui Widgets Core Concurrent Network WebSockets Sql)
find_package(StellarSolver REQUIRED)
find_package(OSTSERVER REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/modules)
include_directories(${INDI_INCLUDE_DIR})
include_directories(${CFITSIO_INCLUDE_DIR})
include_directories(${GSL_INCLUDE_DIRS})
include_directories(${Qt5_DIR})
include_directories(${QT5Declarative_INCLUDE_LIBRARIES} )
include_directories(${QT5Declarative_INCLUDE_DIRS} )
include_directories(${STELLARSOLVER_INCLUDE_DIR})
include_directories(${OST_INCLUDE_DIR}/ost)

message(STATUS "OST_INCLUDE_DIR=${OST_INCLUDE_DIR}")


include(CMakeCommon)



# Focus module
#list (APPEND RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.qrc)
#qt5_add_resources (RCC_SOURCES ${RESOURCES})
add_library(ostfocuser SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/polynomialfit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/polynomialfit.h
    ${RCC_SOURCES}
)
target_link_libraries(ostfocuser PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostfocuser PRIVATE FOCUS_MODULE)


# Indipanel module
add_library(ostindipanel SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/indipanel/indipanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/indipanel/indipanel.cpp
)
target_link_libraries(ostindipanel PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostindipanel PRIVATE INDIPANEL_MODULE)


# guider module
add_library(ostguider SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/guider/guider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/guider/guider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/guider/guider.qrc
)
target_link_libraries(ostguider PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostguider PRIVATE GUIDER_MODULE)

## polar module
#add_library(ostpolar SHARED
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/polar.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/polar.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/rotations.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/rotations.cpp
#)
#target_link_libraries(ostpolar PRIVATE
#    indiclient
#    Qt${QT_VERSION_MAJOR}::Core
#    Qt${QT_VERSION_MAJOR}::Concurrent
#    ${STELLARSOLVER_LIBRARIES}
#    ${INDI_LIBRARIES} ${CFITSIO_LIBRARIES} ${ZLIB_LIBRARY}  ${NOVA_LIBRARIES}
#    ${GSL_LIBRARIES} ${ZLIB_LIBRARY} ${CIMG_LIBRARIES} ${OpenCV_LIBS}
#    z -lpthread
#)
#target_compile_definitions(ostpolar PRIVATE POLAR_MODULE)
#
# inspector module
add_library(ostinspector SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/inspector/inspector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/inspector/inspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/inspector/inspector.qrc
)
target_link_libraries(ostinspector PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostinspector PRIVATE INSPECTOR_MODULE)

# Allsky module
add_library(ostallsky SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/allsky/allsky.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/allsky/allsky.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/allsky/allsky.qrc
)
target_link_libraries(ostallsky PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostallsky PRIVATE ALLSKY_MODULE)

# sequencer module
add_library(ostsequencer SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/sequencer/sequencer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/sequencer/sequencer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/sequencer/sequencer.qrc
)
target_link_libraries(ostsequencer PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostsequencer PRIVATE SEQUENCER_MODULE)

# navigator module
add_library(ostnavigator SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/navigator/navigator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/navigator/navigator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/navigator/navigator.qrc
)
target_link_libraries(ostnavigator PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostnavigator PRIVATE NAVIGATOR_MODULE)

## meteo module
#add_library(ostmeteo SHARED
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.h
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.qrc
#)
#target_link_libraries(ostmeteo PRIVATE
#    ${OST_LIBRARY_INDI}
#    Qt${QT_VERSION_MAJOR}::Core
#    Qt${QT_VERSION_MAJOR}::Concurrent
#    Qt${QT_VERSION_MAJOR}::Widgets
#    Threads::Threads
#    z
#)
#target_compile_definitions(ostmeteo PRIVATE METEO_MODULE)


install(TARGETS
    ostallsky
    ostinspector
    ostnavigator
    ostguider
    ostindipanel
    ostfocuser
    ostsequencer
#    ostmeteo
#    ostpolar
    RUNTIME DESTINATION "/usr/bin"
    LIBRARY DESTINATION "/usr/lib"
)

#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
#message(STATUS "OST_FOUND=${OST_FOUND}")
#message(STATUS "OST_LIBRARY=${OST_LIBRARY}")
#message(STATUS "OST_INCLUDE_DIR=${OST_INCLUDE_DIR}")
#message(STATUS "OST_LIBRARY_BASE=${OST_LIBRARY_BASE}")
#message(STATUS "OST_LIBRARY_INDI=${OST_LIBRARY_INDI}")



