cmake_minimum_required(VERSION 3.6)
project(voxel)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(SOURCE_FILES
        src/main.cpp
        src/Voxel.cpp src/Voxel.hpp
        src/Window.cpp src/Window.hpp
        src/Mouse.cpp src/Mouse.hpp
        src/TinyAlgebra.hpp
        src/Keyboard.cpp src/Keyboard.hpp
        src/World.cpp src/World.hpp
        src/Block.hpp
        src/QuadEBO.cpp src/QuadEBO.hpp
        src/Shader.cpp src/Shader.hpp
        src/Camera.hpp
        src/Player.cpp src/Player.hpp
        src/Texture.cpp src/Texture.hpp
        src/TextureArray.cpp src/TextureArray.hpp
        src/TinyAlgebraExtensions.hpp
        src/Text.cpp src/Text.hpp
        src/Debug.hpp src/Debug.cpp
        src/Profiler.hpp src/Profiler.cpp
        src/Settings.hpp
        src/RingBufferSingleProducerSingleConsumer.hpp
        src/SparseMap.hpp
        src/SphereIterator.hpp
        src/ModTable.hpp
        ../gl3w/gl3w/build/src/gl3w.c
        src/ThreadBarrier.hpp src/MemoryBlockUnit.hpp src/MemoryBlock.hpp)

add_executable(voxel ${SOURCE_FILES})

# for <atomic>
target_link_libraries(voxel atomic)

# glfw3
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
# include_directories(${GLFW_INCLUDE_DIRS})
target_link_libraries(voxel ${GLFW_LIBRARIES})

# pthread ( for c++ <threads> ? )
target_link_libraries(voxel pthread)

# zlib
find_package( ZLIB REQUIRED )
include_directories( ${ZLIB_INCLUDE_DIRS} )
target_link_libraries( voxel ${ZLIB_LIBRARIES} )

# gl3w
#link_directories(/usr/local/lib)
#target_link_libraries (voxel gl3w)
include_directories(../gl3w/gl3w/build/include)

# dl needed by gl3w ( must be included after gl3w ? )
target_link_libraries(voxel ${CMAKE_DL_LIBS})

# soil
link_directories(/usr/lib)
target_link_libraries(voxel SOIL)
