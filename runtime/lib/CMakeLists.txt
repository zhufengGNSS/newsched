add_library(gnuradio-runtime SHARED
  domain_adapter.cpp
  flat_graph.cpp   
  scheduler.cpp
  )
set_target_properties(gnuradio-runtime PROPERTIES
            CXX_STANDARD 17
            )

target_include_directories(gnuradio-runtime
  PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  ${CMAKE_SOURCE_DIR}/blocklib/common/include
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  )

target_link_libraries(gnuradio-runtime 
Volk::volk
ZeroMQ::ZeroMQ
gnuradio-blocklib-blocks)

# ZMQ 4.3.1 deprecated the send and recv with references.  Ignore for now // FIXME
target_compile_options(gnuradio-runtime PRIVATE -Wno-deprecated-declarations)

message(STATUS ${INTERFACE_INCLUDE_DIRECTORIES})
#pybind11_add_module(newschedpy pybind.cpp)
#target_link_libraries(newschedpy PUBLIC newsched)

#install(TARGETS newsched)