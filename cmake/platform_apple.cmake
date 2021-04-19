include("${CMAKE_CURRENT_LIST_DIR}/platform_base.cmake")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")

function(set_source_files)
    set(oneValueArgs RESULT)
    cmake_parse_arguments(APPLE_SSS "" "${oneValueArgs}" "" ${ARGN})
    if(NOT DEFINED APPLE_SSS_RESULT)
        message(FATAL_ERROR "set_source_files_base needs to be called with RESULT")
    endif()

    set_source_files_base(RESULT BASE_SRC_FILES)
    set(
        ${APPLE_SSS_RESULT}
        ${BASE_SRC_FILES}
        Fleece/Integration/ObjC/FleeceDocument.mm
        Fleece/Integration/ObjC/MArray+ObjC.mm
        Fleece/Integration/ObjC/MDict+ObjC.mm
        Fleece/Integration/ObjC/MValue+ObjC.mm
        ObjC/Encoder+ObjC.mm
        ObjC/Fleece+CoreFoundation.mm
        ObjC/slice+CoreFoundation.cc
        ObjC/slice+ObjC.mm
        ObjC/Value+ObjC.mm
        PARENT_SCOPE
    )
endfunction()

function(set_base_platform_files)
     set(oneValueArgs RESULT)
     cmake_parse_arguments(APPLE_SSS "" "${oneValueArgs}" "" ${ARGN})
     if(NOT DEFINED APPLE_SSS_RESULT)
         message(FATAL_ERROR "set_source_files_base needs to be called with RESULT")
     endif()

      set(
         ${APPLE_SSS_RESULT}
         ObjC/slice+CoreFoundation.cc
         ObjC/slice+ObjC.mm
         PARENT_SCOPE
     )
endfunction()

function(set_test_source_files)
    set(oneValueArgs RESULT)
    cmake_parse_arguments(WIN_SSS "" "${oneValueArgs}" "" ${ARGN})
    if(NOT DEFINED WIN_SSS_RESULT)
        message(FATAL_ERROR "set_source_files_base needs to be called with RESULT")
    endif()

    set_test_source_files_base(RESULT BASE_SRC_FILES)
    set(
        ${WIN_SSS_RESULT}
        ${BASE_SRC_FILES}
        Tests/ObjCTests.mm
        Fleece/Integration/ObjC/MTests.mm
        PARENT_SCOPE
    )
endfunction()

function(setup_build)
    target_include_directories(
        FleeceStatic PRIVATE
        Integration/ObjC
    )

    target_include_directories(
        Fleece PRIVATE
        Integration/ObjC
    )

    set_target_properties(
        Fleece PROPERTIES LINK_FLAGS
        "-exported_symbols_list ${PROJECT_SOURCE_DIR}/Fleece/Support/Fleece.exp"
    )

    target_link_libraries(
        FleeceStatic INTERFACE
        "-framework CoreFoundation"
        "-framework Foundation"
    )

    target_compile_definitions(
        FleeceStatic PUBLIC
        -D__STDC_WANT_LIB_EXT1__=1 # For memset_s
    )
endfunction()

function(setup_test_build)

endfunction()
