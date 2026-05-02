# Copy shipped gettext trees next to the desktop executable / into dist.
# Fails the build if the source locale layout or required catalogs are missing.

if(NOT DEFINED LOCALE_SRC_DIR OR NOT DEFINED OUT_LOCALE_DIR)
    message(FATAL_ERROR "LOCALE_SRC_DIR and OUT_LOCALE_DIR are required")
endif()

if(NOT IS_DIRECTORY "${LOCALE_SRC_DIR}")
    message(FATAL_ERROR "Locale source directory is missing: ${LOCALE_SRC_DIR}")
endif()

set(_required_mo "${LOCALE_SRC_DIR}/ru/LC_MESSAGES/owalkie.mo")
if(NOT EXISTS "${_required_mo}")
    message(FATAL_ERROR "Required Russian gettext catalog is missing: ${_required_mo}")
endif()

if(EXISTS "${OUT_LOCALE_DIR}")
    file(REMOVE_RECURSE "${OUT_LOCALE_DIR}")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LOCALE_SRC_DIR}" "${OUT_LOCALE_DIR}"
    RESULT_VARIABLE _owalkie_locale_copy_rc
)
if(NOT _owalkie_locale_copy_rc EQUAL 0)
    message(FATAL_ERROR "Failed to copy locale tree into ${OUT_LOCALE_DIR}")
endif()
