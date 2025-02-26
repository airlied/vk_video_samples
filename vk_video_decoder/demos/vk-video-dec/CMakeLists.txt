set (GLMINC_PREFIX ${PROJECT_SOURCE_DIR}/libs)

macro(generate_dispatch_table out)
    add_custom_command(OUTPUT ${out}
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate-dispatch-table.py ${out}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate-dispatch-table.py
        )
endmacro()

macro(glsl_to_spirv src)
    add_custom_command(OUTPUT ${src}.h
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/glsl-to-spirv ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${CMAKE_CURRENT_SOURCE_DIR}/${src}.h ${GLSLANG_VALIDATOR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/glsl-to-spirv ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${GLSLANG_VALIDATOR}
        )
endmacro()

macro(compile_cuda src)
    get_filename_component(OUT_FILE_NAME ${src} NAME)
    set(CUDA_OUT_FILE_NAME ${OUT_FILE_NAME}.o)
    add_custom_command(OUTPUT ${CUDA_OUT_FILE_NAME}
        COMMAND ${NVCC} -std=c++11 -o ${CMAKE_CURRENT_BINARY_DIR}/${CUDA_OUT_FILE_NAME} -c ${CMAKE_CURRENT_SOURCE_DIR}/${src}
        DEPENDS ${NVCC} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
        )
endmacro()

generate_dispatch_table(${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/HelpersDispatchTable.h)
generate_dispatch_table(${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/HelpersDispatchTable.cpp)

set(sources
    AppDecVulkanFrame/VulkanFrame.cpp
    AppDecVulkanFrame/VulkanFrame.h
    AppDecVulkanFrame/VulkanVideoProcessor.cpp
    AppDecVulkanFrame/VulkanVideoProcessor.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/Main.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/Shell.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellDirect.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/Shell.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/FrameProcessor.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/FrameProcessor.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/NvVkDecoder/NvVkDecoder.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/NvVkDecoder/NvVkDecoder.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VulkanVideoFrameBuffer/VulkanVideoFrameBuffer.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VulkanVideoFrameBuffer/VulkanVideoFrameBuffer.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/Helpers.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/HelpersDispatchTable.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/HelpersDispatchTable.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/VulkanShaderCompiler.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/VulkanShaderCompiler.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/nvVkFormats.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/pattern.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/pattern.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/VulkanVideoUtils.cpp
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkCodecUtils/VulkanVideoUtils.h
    ${EXTERNAL_LIBS_SOURCE_ROOT}/VkVideoParser/VulkanVideoParser.cpp
    )

set(definitions
    PRIVATE -DVK_NO_PROTOTYPES
    PRIVATE -DGLM_FORCE_RADIANS)

set(includes
    PRIVATE ${GLMINC_PREFIX}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set(libraries PRIVATE ${CMAKE_THREAD_LIBS_INIT})

link_directories(
    ${VULKAN_VIDEO_DEVICE_LIBS_PATH}
    ${VULKAN_VIDEO_DEC_LIBS_PATH}
    ${VULKAN_TEGRA_LIBS_PATH}
    ${VULKAN_VIDEO_PARSER_LIB_PATH}
    ${SHADERC_SEARCH_PATH}
    ${LIBNVPARSER_BINARY_ROOT}
    )

if(WIN32)
    list(APPEND libraries PRIVATE ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${VULKAN_VIDEO_PARSER_LIB} ${GLSLANG_LIBRARIES})
else()
    list(APPEND libraries PRIVATE -lX11)
    list(APPEND libraries PRIVATE -lavcodec -lavutil -lavformat)
    list(APPEND libraries PRIVATE -L${SHADERC_SEARCH_PATH} -lshaderc_shared)
    list(APPEND libraries PRIVATE -L${CMAKE_INSTALL_LIBDIR} -l${VULKAN_VIDEO_PARSER_LIB})
    list(APPEND libraries PRIVATE -L${LIBNVPARSER_BINARY_ROOT} -l${VULKAN_VIDEO_PARSER_LIB})
endif()

if(DEFINED ENV{TEGRA_TOP})
    # For the Tegra parser: NvRmChipGetCapabilityU32
    list(APPEND libraries PRIVATE -lnvrm_chip)
    # For the Tegra parser: NvOsGetConfigString
    list(APPEND libraries PRIVATE -lnvos)
endif()

if(DEFINED ENV{TEGRA_TOP})
	list(APPEND definitions PRIVATE -D_FILE_OFFSET_BITS=64)
	list(APPEND definitions PRIVATE -DNV_BUILD_CONFIGURATION_EXPOSING_T19X=1)
	list(APPEND definitions PRIVATE -DTVMR_NVDEC4=1)
endif()

if(TARGET vulkan)
    list(APPEND definitions PRIVATE -DUNINSTALLED_LOADER="$<TARGET_FILE:vulkan>")
endif()

if(WIN32)
    list(APPEND definitions PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
    list(APPEND definitions PRIVATE -DWIN32_LEAN_AND_MEAN)

    list(APPEND sources ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellWin32.cpp ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellWin32.h)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    list(APPEND libraries PRIVATE -ldl -lrt -lpthread)

    if(BUILD_WSI_XCB_SUPPORT AND DEMOS_WSI_SELECTION STREQUAL "XCB")
        find_package(XCB REQUIRED)

        list(APPEND sources ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellXcb.cpp ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellXcb.h)
        list(APPEND definitions PRIVATE -DVK_USE_PLATFORM_XCB_KHR)
        list(APPEND includes PRIVATE ${XCB_INCLUDES})
        list(APPEND libraries PRIVATE ${XCB_LIBRARIES})
    elseif(BUILD_WSI_WAYLAND_SUPPORT AND DEMOS_WSI_SELECTION STREQUAL "WAYLAND")
        find_package(Wayland REQUIRED)

        list(APPEND sources ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellWayland.cpp ${EXTERNAL_LIBS_SOURCE_ROOT}/VkShell/ShellWayland.h)
        list(APPEND definitions PRIVATE -DVK_USE_PLATFORM_WAYLAND_KHR)
        list(APPEND includes PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIR})
        list(APPEND libraries PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..)

list(APPEND includes PRIVATE ${EXTERNAL_LIBS_INCLUDE_ROOT})
list(APPEND includes PRIVATE ${EXTERNAL_LIBS_SOURCE_ROOT})
list(APPEND includes PRIVATE ${VULKAN_VIDEO_PARSER_INCLUDE})
list(APPEND includes PRIVATE ${VULKAN_VIDEO_APIS_INCLUDE})
list(APPEND includes PRIVATE ${VULKAN_VIDEO_APIS_INCLUDE}/vulkan)
list(APPEND includes PRIVATE ${VULKAN_VIDEO_APIS_INCLUDE}/nvidia_utils/vulkan)
list(APPEND includes PRIVATE ${SHADERC_ROOT_PATH}/install/include)

add_executable(vk-video-dec-test ${sources} ${vk-video-dec-test_RESOURCES})
target_compile_definitions(vk-video-dec-test ${definitions})
target_include_directories(vk-video-dec-test ${includes})
target_link_libraries(vk-video-dec-test ${libraries})

install(TARGETS vk-video-dec-test RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})