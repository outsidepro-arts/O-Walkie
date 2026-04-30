if(NOT DEFINED ANDROID_RAW_DIR OR NOT DEFINED OUT_SOUNDS_DIR)
    message(FATAL_ERROR "ANDROID_RAW_DIR and OUT_SOUNDS_DIR are required")
endif()

file(MAKE_DIRECTORY "${OUT_SOUNDS_DIR}")

if(EXISTS "${ANDROID_RAW_DIR}")
    file(GLOB _raw_files "${ANDROID_RAW_DIR}/*")
    if(_raw_files)
        file(COPY ${_raw_files} DESTINATION "${OUT_SOUNDS_DIR}")
    endif()
endif()
