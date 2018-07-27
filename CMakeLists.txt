# ---[ Create project
cmake_minimum_required(VERSION 2.8.0)
cmake_policy(SET CMP0022 NEW)
project(ReverseTale-GameServer)

# ---[ Includes
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-utils)

# -[ Macros and functions
include(Configure)
include(Macros)
include(External)
ResetAllTargets()

# ---[ Create project
CreateTarget(TARGET ReverseTale-GameServer)

# ---[ Find all source and header files
AddToSources(
    TARGET ReverseTale-GameServer
    SRC_PATH "src"
    GLOB_SEARCH ".hpp;.cpp"
    FOLDER_NAME "src"
)


# ---[ Load dependencies
add_subdirectory(dep)


ResolveExternal(TARGET ReverseTale-GameServer)
if (ReverseTale-GameServer_IS_RESOLVED)
	# ---[ Setup include directories
	AddToIncludes(
		TARGET ReverseTale-GameServer
		INC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    # ---[ Dependencies
    AddLibrary(
        TARGET ReverseTale-GameServer
        LIBRARY pthread
        PLATFORM UNIX
    )

    # ---[ Setup some definitions
    set(DEFINITIONS "MONGOCXX_STATIC" "BSONCXX_STATIC" "_SCL_SECURE_NO_WARNINGS")

	BuildNow(
		TARGET ReverseTale-GameServer
		DEFINES ${DEFINITIONS}
		EXECUTABLE
		OUTPUT_NAME GameServer
		C++11
	)

    MakeInstallable(TARGET ReverseTale-GameServer)
endif()
