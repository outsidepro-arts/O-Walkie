# 16 KB page size support (Android 15+ / Pixel with 16KB pages).
# https://developer.android.com/guide/practices/page-sizes
function(owalkie_enable_16kb_page_size target)
    if(NOT ANDROID)
        return()
    endif()
    target_link_options(${target} PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
    )
endfunction()
