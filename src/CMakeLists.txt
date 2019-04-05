#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

add_executable(osmi_water waterinspector.cpp)
target_link_libraries(osmi_water ${OSMIUM_LIBRARIES} ${Boost_LIBRARIES} ${OSMIUMGEOSFACTORY_LIBRARIES} ${GDAL_LIBRARY})
install(TARGETS osmi_water DESTINATION bin)
