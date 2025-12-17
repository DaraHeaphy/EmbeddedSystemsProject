set(SHARED_DIR ${CMAKE_SOURCE_DIR}/../shared)

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)
file(GLOB shared_sources ${SHARED_DIR}/*.c)

idf_component_register(
    SRCS ${app_sources} ${shared_sources}
    INCLUDE_DIRS ${SHARED_DIR}
)
