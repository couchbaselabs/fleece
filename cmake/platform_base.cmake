function(set_source_files_base)
    set(oneValueArgs RESULT)
    cmake_parse_arguments(BASE_SSS "" "${oneValueArgs}" "" ${ARGN})
    if(NOT DEFINED BASE_SSS_RESULT)
        message(FATAL_ERROR "set_source_files_base needs to be called with RESULT")
    endif()

    set(
        ${BASE_SSS_RESULT}
        Fleece/API_Impl/Fleece.cc
        Fleece/API_Impl/FLSlice.cc
        Fleece/Core/Array.cc
        Fleece/Core/DeepIterator.cc
        Fleece/Core/Dict.cc
        Fleece/Core/Doc.cc
        Fleece/Core/Encoder.cc
        Fleece/Core/JSONConverter.cc
        Fleece/Core/JSONDelta.cc
        Fleece/Core/Path.cc
        Fleece/Core/Pointer.cc
        Fleece/Core/SharedKeys.cc
        Fleece/Core/Value+Dump.cc
        Fleece/Core/Value.cc
        Fleece/Integration/MContext.cc
        Fleece/Mutable/HeapArray.cc
        Fleece/Mutable/HeapDict.cc
        Fleece/Mutable/HeapValue.cc
        Fleece/Mutable/ValueSlot.cc
        Fleece/Support/Backtrace.cc
        Fleece/Support/Base64.cc
        Fleece/Support/betterassert.cc
        Fleece/Support/Bitmap.cc
        Fleece/Support/ConcurrentArena.cc
        Fleece/Support/ConcurrentMap.cc
        Fleece/Support/FileUtils.cc
        Fleece/Support/FleeceException.cc
        Fleece/Support/InstanceCounted.cc
        Fleece/Support/NumConversion.cc
        Fleece/Support/JSON5.cc
        Fleece/Support/JSONEncoder.cc
        Fleece/Support/LibC++Debug.cc
        Fleece/Support/ParseDate.cc
        Fleece/Support/RefCounted.cc
        Fleece/Support/slice_stream.cc
        Fleece/Support/sliceIO.cc
        Fleece/Support/StringTable.cc
        Fleece/Support/varint.cc
        Fleece/Support/Writer.cc
        Fleece/Tree/HashTree.cc
        Fleece/Tree/MutableHashTree.cc
        Fleece/Tree/NodeRef.cc
        vendor/jsonsl/jsonsl.c
        vendor/libb64/cdecode.c
        vendor/libb64/cencode.c
        vendor/SwiftDtoa/SwiftDtoa.cc
        PARENT_SCOPE
    )
endfunction()

function(set_test_source_files_base)
    set(oneValueArgs RESULT)
    cmake_parse_arguments(BASE_SSS "" "${oneValueArgs}" "" ${ARGN})
    if(NOT DEFINED BASE_SSS_RESULT)
        message(FATAL_ERROR "set_source_files_base needs to be called with RESULT")
    endif()

    set(
        ${BASE_SSS_RESULT}
        Tests/API_ValueTests.cc
        Tests/DeltaTests.cc
        Tests/EncoderTests.cc
        Tests/FleeceTests.cc
        Tests/FleeceTestsMain.cc
        Tests/HashTreeTests.cc
        Tests/JSON5Tests.cc
        Tests/MutableTests.cc
        Tests/PerfTests.cc
        Tests/SharedKeysTests.cc
        Tests/SupportTests.cc
        Tests/ValueTests.cc
        Experimental/KeyTree.cc
        PARENT_SCOPE
    )
endfunction()
