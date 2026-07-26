function(target_enable_sanitizers target_name)
    if(MSVC)
        return()
    endif()

    if(ORDER_BOOK_ENABLE_TSAN AND (ORDER_BOOK_ENABLE_ASAN OR ORDER_BOOK_ENABLE_UBSAN))
        message(FATAL_ERROR "TSan cannot be combined with ASan/UBSan in this project")
    endif()

    set(sanitizers "")

    if(ORDER_BOOK_ENABLE_ASAN)
        list(APPEND sanitizers "address")
    endif()

    if(ORDER_BOOK_ENABLE_UBSAN)
        list(APPEND sanitizers "undefined")
    endif()

    if(ORDER_BOOK_ENABLE_TSAN)
        list(APPEND sanitizers "thread")
    endif()

    if(sanitizers)
        list(JOIN sanitizers "," sanitizer_list)
        target_compile_options(${target_name} PRIVATE
            -fsanitize=${sanitizer_list}
            -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE
            -fsanitize=${sanitizer_list})
    endif()
endfunction()
