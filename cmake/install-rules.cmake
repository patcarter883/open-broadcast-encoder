install(
    TARGETS open-broadcast-encoder_exe
    RUNTIME COMPONENT Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  # ---- CPack configuration ----
  set(CPACK_PACKAGE_NAME "Open Broadcast Encoder")
  set(CPACK_PACKAGE_VENDOR "Open Broadcast")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Streaming Broadcast Video Encoder")
  set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Open Broadcast Encoder")
  set(CPACK_COMPONENTS_ALL Runtime)

  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # ---- Windows NSIS installer ----
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "Open Broadcast Encoder ${PROJECT_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "Open Broadcast Encoder")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "open-broadcast-encoder.exe")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
      "CreateShortcut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Open Broadcast Encoder.lnk' '$INSTDIR\\\\bin\\\\open-broadcast-encoder.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
      "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Open Broadcast Encoder.lnk'"
    )

    # ---- Bundle MinGW runtime DLLs ----
    foreach(_mingw_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll libssp-0.dll)
      find_file(_found_dll "${_mingw_dll}"
        PATHS /usr/x86_64-w64-mingw32/bin
        PATH_SUFFIXES . 14 13 12
        NO_DEFAULT_PATH
      )
      if(_found_dll)
        install(FILES "${_found_dll}" DESTINATION bin COMPONENT Runtime)
      endif()
      unset(_found_dll CACHE)
    endforeach()

  else()
    set(CPACK_GENERATOR "TGZ")
  endif()

  include(CPack)
endif()
