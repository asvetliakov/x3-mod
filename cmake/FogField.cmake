# Include this module after Python3 is found, then call x3m_add_fog_field_assets(d3d9).
# The root project declares only CXX; RCDATA compilation needs the documented
# CMake resource language enabled before the generated .rc joins the target.
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import numpy; print(numpy.__version__)"
  RESULT_VARIABLE X3M_FOG_NUMPY_STATUS
  OUTPUT_VARIABLE X3M_FOG_NUMPY_VERSION
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT X3M_FOG_NUMPY_STATUS EQUAL 0 OR NOT X3M_FOG_NUMPY_VERSION STREQUAL "2.0.2")
  message(FATAL_ERROR
    "Spatial-fog asset generation requires importable NumPy 2.0.2 in "
    "Python3_EXECUTABLE='${Python3_EXECUTABLE}' (reported "
    "'${X3M_FOG_NUMPY_VERSION}', import exit ${X3M_FOG_NUMPY_STATUS}). "
    "Select a prepared interpreter with "
    "-DPython3_EXECUTABLE=/absolute/path/to/python3; CMake does not install or "
    "replace Python packages.")
endif()
enable_language(RC)

function(x3m_add_fog_field_assets target)
  set(_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/fog_field")
  set(_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/build/bake_fog_fields.py")
  set(_recipe "${CMAKE_CURRENT_SOURCE_DIR}/tools/fog_field_recipe.py")
  set(_blue "${_dir}/bluewell.fogbin")
  set(_green "${_dir}/foggreenoutlands.fogbin")
  set(_meta "${_dir}/fog_field_assets_metadata_inc.h")
  set(_resource "${_dir}/fog_field_assets_resource_inc.h")
  add_custom_command(OUTPUT "${_blue}" "${_green}" "${_meta}" "${_resource}" "${_dir}/manifest.json"
    COMMAND "${Python3_EXECUTABLE}" "${_tool}" --output-dir "${_dir}"
    DEPENDS "${_tool}" "${_recipe}" VERBATIM COMMENT "Baking qualified spatial-fog fields")
  add_custom_target(x3m_fog_field_assets DEPENDS "${_blue}" "${_green}" "${_meta}" "${_resource}" "${_dir}/manifest.json")
  set(X3M_FOG_BLUEWELL_BIN "${_blue}")
  set(X3M_FOG_FOGGREENOUTLANDS_BIN "${_green}")
  file(MAKE_DIRECTORY "${_dir}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fog_field_assets.rc.in" "${_dir}/fog_field_assets.rc" @ONLY)
  set_source_files_properties("${_dir}/fog_field_assets.rc" "${_meta}" "${_resource}" PROPERTIES GENERATED TRUE)
  set_source_files_properties("${_dir}/fog_field_assets.rc" PROPERTIES OBJECT_DEPENDS "${_blue};${_green};${_resource}")
  target_sources(${target} PRIVATE src/renderer/fog_field_assets.cpp "${_dir}/fog_field_assets.rc")
  target_include_directories(${target} PRIVATE "${_dir}")
  add_dependencies(${target} x3m_fog_field_assets)
endfunction()
