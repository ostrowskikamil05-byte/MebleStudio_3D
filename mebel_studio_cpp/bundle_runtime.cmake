# Wywoływany po zbudowaniu: cmake -DEXE=... -DDEST=... -DSEARCH_DIRS=... -DTCL_DIR=... -DTK_DIR=... -DTCL8_DIR=... -P bundle_runtime.cmake
# Kopiuje obok programu biblioteki DLL, których potrzebuje (g++, Tcl/Tk, OpenCASCADE...), oraz skrypty Tcl/Tk.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    DIRECTORIES ${SEARCH_DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss]ystem32" "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss]ys[Ww][Oo][Ww]64")
foreach(dll IN LISTS resolved)
    file(COPY "${dll}" DESTINATION "${DEST}")
endforeach()
list(LENGTH resolved count)
message(STATUS "Skopiowano ${count} bibliotek DLL do ${DEST}")
foreach(dll IN LISTS unresolved)
    message(STATUS "Nie znaleziono (pomijam): ${dll}")
endforeach()
foreach(pair "tcl|${TCL_DIR}" "tk|${TK_DIR}" "tcl8|${TCL8_DIR}")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 1 source)
    if(source AND EXISTS "${source}")
        get_filename_component(name "${source}" NAME)
        file(COPY "${source}" DESTINATION "${DEST}/lib")
        message(STATUS "Skopiowano skrypty ${name} do ${DEST}/lib")
    endif()
endforeach()
