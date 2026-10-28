# The tab's class ID twice: the INF registers card/driver.card's control_clsid,
# and cpl/main.c's DllGetClassObject answers clsid_tab. If they ever differ the
# Advanced dialog's VGPU98 tab silently goes.
#   cmake -DCARD=<driver.card> -DSOURCE=<cpl/main.c> -P cpl-clsid-check.cmake

file(READ ${CARD} card)
if(NOT card MATCHES "control_clsid[ \t]*=[ \t]*{([0-9A-F]+)-([0-9A-F]+)-([0-9A-F]+)-([0-9A-F][0-9A-F])([0-9A-F][0-9A-F])-([0-9A-F]+)}")
    message(FATAL_ERROR "no control_clsid in ${CARD}")
endif()
set(d1 ${CMAKE_MATCH_1})
set(d2 ${CMAKE_MATCH_2})
set(d3 ${CMAKE_MATCH_3})
set(b0 ${CMAKE_MATCH_4})
set(b1 ${CMAKE_MATCH_5})
set(rest ${CMAKE_MATCH_6})

# Data4: the fourth group's two bytes, then the fifth group's six.
set(bytes "0x${b0}, 0x${b1}")
foreach(i 0 2 4 6 8 10)
    string(SUBSTRING ${rest} ${i} 2 byte)
    string(APPEND bytes ", 0x${byte}")
endforeach()
set(want "{ 0x${d1}, 0x${d2}, 0x${d3}, { ${bytes} } }")

file(READ ${SOURCE} source)
string(FIND "${source}" "clsid_tab = ${want};" at)
if(at EQUAL -1)
    message(FATAL_ERROR "${SOURCE}'s clsid_tab is not ${CARD}'s control_clsid: expected\n  clsid_tab = ${want};")
endif()
message(STATUS "clsid_tab matches control_clsid: ${want}")
