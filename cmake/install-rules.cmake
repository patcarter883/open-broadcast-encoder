install(
    TARGETS open-broadcast-encoder_exe
    RUNTIME COMPONENT open-broadcast-encoder_Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
