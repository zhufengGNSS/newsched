########################################################################
# Install header files
########################################################################
install(FILES
  api.h
  attributes.h
  flowgraph.hpp
  scheduler.hpp
  DESTINATION ${GR_INCLUDE_DIR}/gnuradio
)

message(STATUS ${CMAKE_INSTALL_INCLUDEDIR})