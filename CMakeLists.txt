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
find_package(OpenCV REQUIRED )
find_package(WCSLIB REQUIRED)
find_package(QT NAMES Qt5 COMPONENTS Gui Widgets Core Concurrent Network WebSockets Sql Scxml Multimedia Qml REQUIRED)
find_package(Qt5 5.15.2 REQUIRED COMPONENTS Gui Widgets Core Concurrent Network WebSockets Sql Multimedia Scxml Qml)
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
include_directories(${STELLARSOLVER_INCLUDE_DIR})
include_directories(${OST_INCLUDE_DIR}/ost)

message(STATUS "OST_INCLUDE_DIR=${OST_INCLUDE_DIR}")


include(CMakeCommon)

# Generate versionModule.cc at build time (not configure time)
# This ensures version information is always up-to-date with each build

# Create a custom target that always runs
add_custom_target(
    generate_version ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/generate-version.sh"
            "${CMAKE_CURRENT_BINARY_DIR}/versionModule.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/versionModule.cc.in"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/versionModule.cc"
    COMMENT "Generating versionModule.cc from git information..."
    VERBATIM
)

# Add versionModule.cc to sources
list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/versionModule.cc" versionModule.hh)

message(STATUS "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
message(STATUS "Version file will be generated at build time: ${CMAKE_CURRENT_BINARY_DIR}/versionModule.cc")

# Focus module
#list (APPEND RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.qrc)
#qt5_add_resources (RCC_SOURCES ${RESOURCES})
add_library(ostfocus SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/focus.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/polynomialfit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/focus/polynomialfit.h
    ${RCC_SOURCES}
)
target_link_libraries(ostfocus PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Scxml
    Threads::Threads
    z
)
target_compile_definitions(ostfocus PRIVATE FOCUS_MODULE)

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

# polar module
add_library(ostpolar SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/polar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/polar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/rotations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/rotations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/polar/polar.qrc
)
target_link_libraries(ostpolar PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostpolar PRIVATE POLAR_MODULE)

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

# meteo module
add_library(ostmeteo SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/meteo/meteo.qrc
)
target_link_libraries(ostmeteo PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostmeteo PRIVATE METEO_MODULE)

# Blind periodic error correction module
add_library(ostblindpec SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/blindpec.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/blindpec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/blindpec.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/CV_SubPix.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/CV_SubPix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/tacquisitionvideo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/blindpec/tacquisitionvideo.cpp
)
target_link_libraries(ostblindpec PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    ${OpenCV_LIBS}
    Threads::Threads
    z
)
target_compile_definitions(ostblindpec PRIVATE BLINDPEC_MODULE)

# darkassist module
add_library(ostdarkassist SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/darkassist/darkassist.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/darkassist/darkassist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/darkassist/darkassist.qrc
)
target_link_libraries(ostdarkassist PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostdarkassist PRIVATE DARKASSIST_MODULE)

# planner module
add_library(ostplanner SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/planner/planner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/planner/planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/planner/planner.qrc
)
target_link_libraries(ostplanner PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostplanner PRIVATE PLANNER_MODULE)

# template module - skeleton for creating new modules
add_library(osttemplate SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/template/template.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/template/template.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/template/template.qrc
)
target_link_libraries(osttemplate PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(osttemplate PRIVATE TEMPLATE_MODULE)

install(TARGETS
    ostallsky
    ostinspector
    ostnavigator
    ostguider
    ostfocus
    ostsequencer
    ostmeteo
    ostpolar
    ostblindpec
    ostdarkassist
    ostplanner
#    osttemplate #don't install template ...
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



