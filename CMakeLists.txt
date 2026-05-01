cmake_minimum_required(VERSION 3.16)

project(worksheet6 VERSION 1.0.0 LANGUAGES CXX)

# Install locally instead of Program Files during development
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Avoid Vulkan warning if Vulkan SDK is not installed
set(VTK_USE_VULKAN OFF CACHE BOOL "" FORCE)
set(VTK_ENABLE_VR_SUPPORT ON CACHE BOOL "" FORCE)

# OpenVR paths: set these in CMake GUI if different
set(OpenVR_INCLUDE_DIR "C:/OpenVR/headers" CACHE PATH "OpenVR include directory")
set(OpenVR_LIBRARY "C:/OpenVR/lib/win64/openvr_api.lib" CACHE FILEPATH "OpenVR library")
set(OpenVR_DLL "C:/OpenVR/bin/win64/openvr_api.dll" CACHE FILEPATH "OpenVR runtime DLL")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGLWidgets)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersSources
    FiltersGeometry
    IOGeometry
    ImagingCore
    RenderingCore
    RenderingOpenGL2
    RenderingOpenVR
    GUISupportQt
    RenderingQt
)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/ModelPart.cpp
    src/ModelPart.h
    src/ModelPartList.cpp
    src/ModelPartList.h
    src/optiondialog.cpp
    src/optiondialog.h
    src/optiondialog.ui
    src/VRRenderThread.cpp
    src/VRRenderThread.h
    src/icons.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(worksheet6
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(worksheet6
        ${PROJECT_SOURCES}
    )
endif()

target_include_directories(worksheet6 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenVR_INCLUDE_DIR}
)

target_link_libraries(worksheet6 PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    ${VTK_LIBRARIES}
    ${OpenVR_LIBRARY}
)

vtk_module_autoinit(
    TARGETS worksheet6
    MODULES ${VTK_LIBRARIES}
)

set_target_properties(worksheet6 PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Copy vrbindings to build output
add_custom_target(VRBindings ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/vrbindings
        ${CMAKE_BINARY_DIR}/vrbindings
)

add_dependencies(worksheet6 VRBindings)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(worksheet6)
endif()

include(GNUInstallDirs)

install(TARGETS worksheet6
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY
    ${CMAKE_SOURCE_DIR}/vrbindings/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/vrbindings
)

# Install CAD models if Level0 exists
if(EXISTS "${CMAKE_SOURCE_DIR}/Level0")
    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/Level0/"
        DESTINATION Level0
    )
endif()

# Deploy Qt DLLs automatically
if(QT_VERSION_MAJOR EQUAL 6)
    qt_generate_deploy_app_script(
        TARGET worksheet6
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

# Install OpenVR runtime DLL
if(EXISTS "${OpenVR_DLL}")
    install(FILES
        "${OpenVR_DLL}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Install VTK runtime DLLs
if(WIN32)
    file(GLOB VTK_DLLS
        "C:/Program Files (x86)/VTK/bin/*.dll"
    )

    install(FILES
        ${VTK_DLLS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

include(InstallRequiredSystemLibraries)

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "FinalProject")
set(CPACK_PACKAGE_VENDOR "EEEE2076 Group Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt VTK OpenVR CAD Viewer")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FinalProject")

set(CPACK_NSIS_DISPLAY_NAME "FinalProject")
set(CPACK_NSIS_PACKAGE_NAME "FinalProject")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

set(CPACK_NSIS_MENU_LINKS
    "bin/worksheet6.exe" "FinalProject"
    "Uninstall.exe" "Uninstall FinalProject"
)

set(CPACK_CREATE_DESKTOP_LINKS "worksheet6")

include(CPack)