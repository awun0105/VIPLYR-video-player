# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/Viplyr_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/Viplyr_autogen.dir/ParseCache.txt"
  "Viplyr_autogen"
  )
endif()
