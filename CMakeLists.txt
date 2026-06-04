cmake_minimum_required(VERSION 3.16)
project(SeqChecker VERSION 0.101 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuracion de build por defecto
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
if(WIN32)
    if(MSVC)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi")
    else()
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

if(WIN32)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64")
endif()

find_package(Qt6 COMPONENTS Core Gui Widgets Concurrent Svg REQUIRED)

# Recursos
set(RESOURCES
    resources/resources.qrc
)

# Icono para Windows
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/SeqChecker.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_icon.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc" @ONLY)
    set(RESOURCES ${RESOURCES} "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc")
endif()

# Archivos fuente
set(SOURCES
    src/main.cpp
    src/ui/mainwindow/mainwindow.cpp
    src/core/AppPathManager.cpp
    src/core/debug/debug_flags.cpp
    src/utils/PythonRunner.cpp
    src/utils/LogRotation.cpp
)

# Headers
set(HEADERS
    include/seqchecker/mainwindow.h
    include/mediatools/AppPathManager.h
    include/mediatools/debug_flags.h
    include/mediatools/PythonRunner.h
    include/mediatools/utils/LogRotation.h
)

# Crear el ejecutable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Vincular con Qt
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SEQCHECKER_VERSION="${PROJECT_VERSION}"
)

# Vincular con bibliotecas específicas de Windows
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 ole32)
endif()

# Incluir directorios
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configuración para Windows: ocultar consola en release
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Instalación
if(WIN32)
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION bin)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/python_runtime DESTINATION bin)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/py_scr DESTINATION bin)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/thirdparty DESTINATION bin)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/config DESTINATION bin)
    install(DIRECTORY DESTINATION bin/logs)
endif()
