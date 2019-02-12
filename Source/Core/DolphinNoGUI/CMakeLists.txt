if(NOT((ENABLE_X11 AND X11_FOUND) OR ENABLE_HEADLESS))
  return()
endif()

add_executable(dolphin-zek-nogui
  MainNoGUI.cpp
)

set_target_properties(dolphin-zek-nogui PROPERTIES OUTPUT_NAME dolphin-zek-nogui)

target_link_libraries(dolphin-zek-nogui
PRIVATE
  core
  uicommon
  cpp-optparse
)

if(USE_DISCORD_PRESENCE)
  target_compile_definitions(dolphin-zek-nogui PRIVATE -DUSE_DISCORD_PRESENCE)
endif()

set(CPACK_PACKAGE_EXECUTABLES ${CPACK_PACKAGE_EXECUTABLES} dolphin-nogui)
install(TARGETS dolphin-nogui RUNTIME DESTINATION ${bindir})

