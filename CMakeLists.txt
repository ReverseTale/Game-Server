# ---[ Create project
project(ReverseTale-GameServer)

# ---[ Parse all prefixes
include(cmake-common/Utils.cmake)
String2List(${CMAKE_PREFIX_PATH} CMAKE_PREFIX_PATH)
String2List(${CMAKE_MODULE_PATH} CMAKE_MODULE_PATH)

# ---[ Find all source and header files
file(GLOB_RECURSE sources_Main      src/*.cpp     src/*.h)
set(GameServer_SOURCES
    ${sources_Main}
)

# ---[ Load dependencies
if (${ReverseTale_DEV})
    include_directories(${CMAKE_SOURCE_DIR}/src/Library/include)
    include_directories(${CMAKE_SOURCE_DIR}/src/Common-Server/src)
else()
    find_package(ReverseTale-Library        REQUIRED)
    find_package(ReverseTale-CommonServer   REQUIRED)
endif()

# ---[ Add definitions and executable
add_definitions(-DMONGOCXX_STATIC -DBSONCXX_STATIC -D_SCL_SECURE_NO_WARNINGS)
add_executable(ReverseTale-GameServer ${GameServer_SOURCES})

# ---[ Link dependencies libraries
target_link_libraries(ReverseTale-GameServer
    ReverseTale-CommonServer
)

# ---[ Set C++11 standard
set_property(TARGET ReverseTale-GameServer PROPERTY CXX_STANDARD 11)
target_compile_features(ReverseTale-GameServer PRIVATE cxx_decltype cxx_nullptr cxx_variadic_templates)

# ---[ Setup output directory and include directories
set_target_properties(ReverseTale-GameServer PROPERTIES
    OUTPUT_NAME GameServer
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/../bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/../bin)

include(CMakePackageConfigHelpers)
set(config_install_dir lib/cmake/ReverseTale-GameServer)
set(version_config ${PROJECT_BINARY_DIR}/ReverseTale-GameServer-config-version.cmake)
set(project_config ${PROJECT_BINARY_DIR}/ReverseTale-GameServer-config.cmake)
set(targets_export_name ReverseTale-GameServer-targets)

# Generate the version, config and target files into the build directory.
write_basic_package_version_file(
  ${version_config}
  VERSION 0.0.1
  COMPATIBILITY AnyNewerVersion)
configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/config.cmake.in
  ${project_config}
  INSTALL_DESTINATION ${config_install_dir})
export(TARGETS ReverseTale-GameServer FILE ${PROJECT_BINARY_DIR}/${targets_export_name}.cmake)

install(TARGETS ReverseTale-GameServer
	EXPORT ${targets_export_name}
	RUNTIME DESTINATION bin/
  	LIBRARY DESTINATION lib/
  	ARCHIVE DESTINATION lib/)

install(FILES ${project_config} ${version_config} DESTINATION ${config_install_dir})
install(EXPORT ${targets_export_name} DESTINATION ${config_install_dir})
