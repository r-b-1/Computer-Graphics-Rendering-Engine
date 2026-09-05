add_library (cs4212-util
  ArgumentParsing.cpp ArgumentParsing.h
  handleGraphicsArgs.cpp handleGraphicsArgs.h
  model_obj.cpp model_obj.h
)
target_compile_definitions(cs4212-util PUBLIC HAS_GLM)
target_link_libraries(cs4212-util PRIVATE Boost::program_options)
target_link_libraries(cs4212-util PUBLIC glm::glm)

# Throttled branch: half/half Daft Punk helmet with volumetric laser smoke show.
add_executable(daftpunk_lasershow
  main.cc
)
target_include_directories(daftpunk_lasershow PRIVATE ${CMAKE_SOURCE_DIR}/src)

