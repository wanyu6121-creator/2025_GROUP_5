cmake_minimum_required(VERSION 3.16)

project(worksheet6 VERSION 0.1 LANGUAGES CXX)

# Disable Vulkan (Vulkan SDK not installed; suppresses WrapVulkanHeaders warnings)
set(VTK_USE_VULKAN OFF CACHE BOOL "" FORCE)
set(VTK_ENABLE_VR_SUPPORT ON CACHE BOOL "" FORCE)

set(OpenVR_INCLUDE_DIR "C:/Users/adria/openvr-master/openvr-master/headers")
set(OpenVR_LIBRARY "C:/Users/adria/openvr-master/openvr-master/lib/win64/openvr_api.lib")

# Explicitly specify required VTK components including OpenVR
find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    FiltersSources
    FiltersGeometry
    FiltersCore
    IOGeometry
    ImagingCore
    RenderingCore
    RenderingOpenGL2
    RenderingOpenVR
    GUISupportQt
    RenderingQt
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGLWidgets)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        ModelPart.cpp
        ModelPart.h
        ModelPartList.cpp
        ModelPartList.h
        icons.qrc
        VRRenderThread.h
        VRRenderThread.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(worksheet6
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        optiondialog.h optiondialog.cpp optiondialog.ui
    )
    target_include_directories(worksheet6 PRIVATE
     "${OPENVR_ROOT}/headers"
    )
else()
    if(ANDROID)
        add_library(worksheet6 SHARED ${PROJECT_SOURCES})
    else()
        add_executable(worksheet6 ${PROJECT_SOURCES})
    endif()
endif()

target_link_libraries(worksheet6 PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    ${VTK_LIBRARIES}
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.worksheet6)
endif()
set_target_properties(worksheet6 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS worksheet6
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(worksheet6)
endif()

# ---------------------------------------------------------------------------
# Copy vrbindings into the build output directory.
#
# CMAKE_CURRENT_SOURCE_DIR is the src/ folder where this CMakeLists.txt lives.
# vrbindings/ is kept one level up at the repository root, so we use ../vrbindings.
# If you have moved vrbindings/ inside src/ instead, change the path to:
#   ${CMAKE_CURRENT_SOURCE_DIR}/vrbindings
# ---------------------------------------------------------------------------
add_custom_target(VRBindings)
add_custom_command(TARGET VRBindings PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E
    copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../vrbindings
        ${CMAKE_BINARY_DIR}/vrbindings)

# Ensure vrbindings are copied before the main target is built
add_dependencies(worksheet6 VRBindings)

# Run vtk_module_autoinit to register all VTK modules correctly
vtk_module_autoinit(
    TARGETS worksheet6
    MODULES ${VTK_LIBRARIES}
)
