find_package(OpenCV REQUIRED)
find_package(HVToolkit REQUIRED)
find_package(MetavisionSDK REQUIRED COMPONENTS base)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH /usr/local/share/cmake/pybind11) # cmd include's path

option(COMPILE_PYTHON3_BINDINGS "Compile python 3 bindings" ON)  # for open the python3.cmake's custom function
include(python3)  # it define the function add_hal_python_bindings
include(pybind11Config) # a package to transform the interface from cpp to python

option(GENERATE_DOC_PYTHON_BINDINGS "Generate python bindings documentation from C++" OFF)




add_library(metavision_utils_pybind INTERFACE)
add_library(MetavisionUtils::pybind ALIAS metavision_utils_pybind) # Create alias target to refer to
target_include_directories(metavision_utils_pybind
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION include
        )



if(GENERATE_DOC_PYTHON_BINDINGS)
    set(hal_python_dependencies metavision_hal_python_doc_from_cpp)
    set(hal_python_include_directories ${GENERATE_PYTHON_BINDINGS_DOC_DIRECTORY})
    set(hal_python_compile_definitions GENERATE_DOC_PYTHON_BINDINGS_USING_CPP_COMMENTS)
endif()


add_hal_python_bindings(
    hv_camera_python
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/hv_camera_python.cpp
    LINK_LIBRARIES
    PUBLIC
    HVToolkit::hv_camera
    PRIVATE
    MetavisionUtils::pybind
    INCLUDE_DIRECTORIES PRIVATE ${hal_python_include_directories} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    COMPILE_DEFINITIONS PRIVATE ${hal_python_compile_definitions}
    DEPENDENCIES ${hal_python_dependencies}
)

add_hal_python_bindings(
    hv_evt2_codec_python 
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/hv_evt2_codec_python.cpp
    LINK_LIBRARIES
    PUBLIC
#    HVToolkit::hv_evt2_codec
   /usr/local/lib/libhv_evt2_codec.so
    ${MetavisionSDK_LIBRARIES}
    INCLUDE_DIRECTORIES PRIVATE ${hal_python_include_directories} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    COMPILE_DEFINITIONS PRIVATE ${hal_python_compile_definitions}
    DEPENDENCIES ${hal_python_dependencies}
)


add_hal_python_bindings(
    hv_event_reader_python 
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/hv_event_reader_python.cpp
    LINK_LIBRARIES
    PUBLIC
    HVToolkit::hv_event_reader
    ${MetavisionSDK_LIBRARIES}
    PRIVATE
    INCLUDE_DIRECTORIES PRIVATE ${hal_python_include_directories} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    COMPILE_DEFINITIONS PRIVATE ${hal_python_compile_definitions}
    DEPENDENCIES ${hal_python_dependencies}
)

add_hal_python_bindings(
    hv_event_writer_python
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/hv_event_writer_python.cpp
    LINK_LIBRARIES
    PUBLIC
    HVToolkit::hv_event_writer
    ${MetavisionSDK_LIBRARIES}
    PRIVATE
    MetavisionUtils::pybind
    INCLUDE_DIRECTORIES PRIVATE ${hal_python_include_directories} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    COMPILE_DEFINITIONS PRIVATE ${hal_python_compile_definitions}
    DEPENDENCIES ${hal_python_dependencies}
)


# 调用 Python 脚本获取路径
execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/get_prefered_site_packages.py --site-type local
    OUTPUT_VARIABLE PYTHON_SITE_PACKAGES_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE  # 去除末尾换行符
)

message(STATUS "Python site-packages 目录: ${PYTHON_SITE_PACKAGES_DIR}")




install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/build/output_modules/hv_camera_python.so
        ${CMAKE_CURRENT_SOURCE_DIR}/build/output_modules/hv_event_reader_python.so
        ${CMAKE_CURRENT_SOURCE_DIR}/build/output_modules/hv_event_writer_python.so
        ${CMAKE_CURRENT_SOURCE_DIR}/build/output_modules/hv_evt2_codec_python.so
    DESTINATION ${PYTHON_SITE_PACKAGES_DIR}
    PERMISSIONS 
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)
