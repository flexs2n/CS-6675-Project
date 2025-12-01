cmake_minimum_required(VERSION 3.16)
project(crackstore_distributed CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED grpc++)

# Find the grpc_cpp_plugin binary (installed by protobuf-compiler-grpc)
find_program(GRPC_CPP_PLUGIN NAMES grpc_cpp_plugin PATHS /usr/bin /usr/lib/grpc)
if(NOT GRPC_CPP_PLUGIN)
  message(FATAL_ERROR "grpc_cpp_plugin not found")
endif()

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GEN_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(PROTO_FILE ${PROTO_DIR}/crackstore.proto)

# Generate protobuf and gRPC sources
add_custom_command(
  OUTPUT
    ${GEN_DIR}/crackstore.pb.cc
    ${GEN_DIR}/crackstore.pb.h
    ${GEN_DIR}/crackstore.grpc.pb.cc
    ${GEN_DIR}/crackstore.grpc.pb.h
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    --proto_path=${PROTO_DIR}
    --cpp_out=${GEN_DIR}
    --grpc_out=${GEN_DIR}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating Protobuf and gRPC C++ sources"
)

add_library(crackstore_proto
  ${GEN_DIR}/crackstore.pb.cc
  ${GEN_DIR}/crackstore.grpc.pb.cc
)
target_include_directories(crackstore_proto PUBLIC
  ${GEN_DIR}
  ${Protobuf_INCLUDE_DIRS}
  ${GRPCPP_INCLUDE_DIRS}
)
target_link_libraries(crackstore_proto PUBLIC
  ${Protobuf_LIBRARIES}
  ${GRPCPP_LIBRARIES}
)

# Executables

# Test Engine
add_executable(test_engine core/test_engine.cpp)
target_link_libraries(test_engine PRIVATE crackstore_proto ${GRPCPP_LIBRARIES} ${Protobuf_LIBRARIES})

# Test Proto
add_executable(test_proto proto/test_proto.cpp)
target_include_directories(test_proto PRIVATE ${GEN_DIR})
target_link_libraries(test_proto PRIVATE crackstore_proto ${GRPCPP_LIBRARIES} ${Protobuf_LIBRARIES})

# Storage Node
add_executable(storage_node storage/storage_node.cpp)
target_include_directories(storage_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(storage_node PRIVATE 
    crackstore_proto 
    ${GRPCPP_LIBRARIES} 
    ${Protobuf_LIBRARIES}
)

# Coordinator
add_executable(coordinator coordinator/coordinator.cpp)
target_link_libraries(coordinator PRIVATE crackstore_proto ${GRPCPP_LIBRARIES} ${Protobuf_LIBRARIES})

# Client
add_executable(client client/client.cpp)
target_link_libraries(client PRIVATE crackstore_proto ${GRPCPP_LIBRARIES} ${Protobuf_LIBRARIES})