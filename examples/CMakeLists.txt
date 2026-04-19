find_library(PCAP_LIBRARY pcap)

if (NOT PCAP_LIBRARY)
    message(STATUS "Could not find libpcap - some examples will not be built")
endif()

add_executable(simplegrep simplegrep.c)
if(MSVC)
    set_source_files_properties(simplegrep.c PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(simplegrep.c PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(simplegrep hs)

add_executable(dump_nfa dump_nfa.cpp)
if(MSVC)
    set_source_files_properties(dump_nfa.cpp PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(dump_nfa.cpp PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(dump_nfa hs)

add_executable(dump_rose dump_rose.cpp utils/string_processing.cpp)
if(MSVC)
    set_source_files_properties(dump_rose.cpp PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(dump_rose.cpp PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(dump_rose hs)
target_include_directories(dump_rose PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(dump dump.cpp)
if(MSVC)
    set_source_files_properties(dump.cpp PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(dump.cpp PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(dump hs)

if (PCAP_LIBRARY AND NOT WIN32)
add_executable(pcapscan pcapscan.cc)
if(MSVC)
    set_source_files_properties(pcapscan.cc PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(pcapscan.cc PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(pcapscan hs pcap)
endif()

if (PCAP_LIBRARY AND NOT WIN32)
add_executable(patbench patbench.cc)
if(MSVC)
    set_source_files_properties(patbench.cc PROPERTIES COMPILE_FLAGS
        "/wd4100")
else()
    set_source_files_properties(patbench.cc PROPERTIES COMPILE_FLAGS
        "-Wall -Wno-unused-parameter")
endif()
target_link_libraries(patbench hs pcap)
endif()

install(FILES simplegrep.c pcapscan.cc patbench.cc README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)