cmake_minimum_required(VERSION 3.20)
project(MebelStudio3D_Cpp VERSION 0.16 LANGUAGES CXX)

# Pełna wersja C++20 programu (tłumaczenie app/*.py 1:1). Docelowa instalacja: MSYS2 UCRT64.
#
#   pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,tk,opencascade,nlohmann-json}
#   (opcjonalnie: mingw-w64-ucrt-x86_64-vtk  - rendering GPU przez VTK)
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   build/bin/MebelStudio3D_cpp.exe
#
# Pliki MebelModel.dll i MebelNative3D.dll są ładowane w czasie działania; CMake kopiuje je obok programu
# z katalogu MEBEL_NATIVE_DIR (domyślnie: katalog nadrzędny, czyli korzeń repozytorium).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ccache (jeśli jest zainstalowany): ponowne budowanie niezmienionych plików trwa sekundy.
find_program(MEBEL_CCACHE ccache)
if(MEBEL_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${MEBEL_CCACHE}")
endif()
set(CMAKE_CXX_EXTENSIONS ON)   # gnu++20: nagłówki MinGW (M_PI, winuser.h) wymagają niezdefiniowanego __STRICT_ANSI__
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(MEBEL_GUI_SUBSYSTEM "Podsystem Windows GUI zamiast konsoli (konsola pokazuje komunikaty błędów)" OFF)
option(MEBEL_LAUNCHER "Punkt wejścia z launcher.cpp (--self-test); OFF = prosty main() z app.cpp" ON)
option(MEBEL_BUNDLE_RUNTIME "Po zbudowaniu kopiuj obok programu wymagane DLL i skrypty Tcl/Tk" ON)
option(MEBEL_BUILD_TK_TESTS "Zbuduj test warstwy Tk na atrapie interpretera Tcl" OFF)
set(MEBEL_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Katalog z MebelModel.dll i MebelNative3D.dll")

# ---------------------------------------------------------------- zależności
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

find_package(OpenCASCADE REQUIRED)
set(MEBEL_OCCT_LIBS "")
foreach(toolkit TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool
                TKFillet TKOffset TKMesh TKShHealing TKXSBase TKDESTEP TKSTEP TKDESTL TKSTL)
    if(TARGET ${toolkit})
        list(APPEND MEBEL_OCCT_LIBS ${toolkit})
    endif()
endforeach()
if(NOT MEBEL_OCCT_LIBS)
    set(MEBEL_OCCT_LIBS ${OpenCASCADE_LIBRARIES})   # starsze konfiguracje bez zdefiniowanych celów
endif()
message(STATUS "OpenCASCADE: ${OpenCASCADE_INCLUDE_DIR} (${MEBEL_OCCT_LIBS})")

# Tcl/Tk 8.6 (nagłówki trafiają WYŁĄCZNIE do tkcore.cpp - nagłówki X11 z tk.h definiują makra None/Bool/Status)
find_path(MEBEL_TCL_INCLUDE_DIR tcl.h PATH_SUFFIXES tcl8.6 tcl9.0 tcl)
find_path(MEBEL_TK_INCLUDE_DIR tk.h PATH_SUFFIXES tcl8.6 tk8.6 tk9.0 tk)
find_library(MEBEL_TCL_LIBRARY NAMES tcl86 tcl8.6 tcl90 tcl9.0 tcl PATH_SUFFIXES lib)
find_library(MEBEL_TK_LIBRARY NAMES tk86 tk8.6 tk90 tk9.0 tk PATH_SUFFIXES lib)
foreach(var MEBEL_TCL_INCLUDE_DIR MEBEL_TK_INCLUDE_DIR MEBEL_TCL_LIBRARY MEBEL_TK_LIBRARY)
    if(NOT ${var})
        message(FATAL_ERROR "Nie znaleziono ${var}. Zainstaluj: pacman -S mingw-w64-ucrt-x86_64-tk")
    endif()
endforeach()
message(STATUS "Tcl: ${MEBEL_TCL_LIBRARY}  Tk: ${MEBEL_TK_LIBRARY}")
find_path(MEBEL_TCL_SCRIPT_DIR init.tcl PATH_SUFFIXES lib/tcl8.6 tcl8.6 lib/tcl9.0 tcl9.0 NO_DEFAULT_PATH
          HINTS "${MEBEL_TCL_INCLUDE_DIR}/.." "${MEBEL_TCL_INCLUDE_DIR}/../..")
find_path(MEBEL_TK_SCRIPT_DIR tk.tcl PATH_SUFFIXES lib/tk8.6 tk8.6 lib/tk9.0 tk9.0 NO_DEFAULT_PATH
          HINTS "${MEBEL_TCL_INCLUDE_DIR}/.." "${MEBEL_TCL_INCLUDE_DIR}/../..")
if(MEBEL_TCL_SCRIPT_DIR)
    get_filename_component(MEBEL_TCL8_SCRIPT_DIR "${MEBEL_TCL_SCRIPT_DIR}/../tcl8" ABSOLUTE)
endif()

# VTK (opcjonalnie): bez niego GPUViewport zgłasza GPUUnavailable i program wraca do rysowania CPU.
set(MEBEL_VTK_MODULES CommonCore CommonDataModel CommonMath CommonTransforms FiltersCore RenderingCore RenderingOpenGL2)
find_package(VTK COMPONENTS ${MEBEL_VTK_MODULES} QUIET)
if(VTK_FOUND)
    message(STATUS "VTK ${VTK_VERSION}: rendering GPU włączony")
else()
    message(STATUS "VTK nie znaleziony: rendering GPU wyłączony (CPU + MebelNative3D.dll działają)")
endif()

# ---------------------------------------------------------------- źródła
file(GLOB MEBEL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(WIN32)
    enable_language(RC)
    list(APPEND MEBEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mebel_studio.rc")
endif()

add_executable(MebelStudio3D_cpp ${MEBEL_SOURCES})
set_target_properties(MebelStudio3D_cpp PROPERTIES OUTPUT_NAME "MebelStudio3D_cpp")

# Nagłówki Tcl/Tk tylko dla jednego pliku
# -idirafter (a nie -I): katalog include toolchaina jako -I psuje #include_next <stdlib.h> w libstdc++
set_source_files_properties(tkcore.cpp PROPERTIES
    COMPILE_OPTIONS "-idirafter;${MEBEL_TCL_INCLUDE_DIR};-idirafter;${MEBEL_TK_INCLUDE_DIR}")

target_include_directories(MebelStudio3D_cpp SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(MebelStudio3D_cpp PRIVATE nlohmann_json::nlohmann_json ${MEBEL_OCCT_LIBS}
                      ${MEBEL_TK_LIBRARY} ${MEBEL_TCL_LIBRARY})
target_compile_definitions(MebelStudio3D_cpp PRIVATE UNICODE _UNICODE NOMINMAX _USE_MATH_DEFINES)
if(MEBEL_LAUNCHER)
    target_compile_definitions(MebelStudio3D_cpp PRIVATE MEBEL_LAUNCHER)
endif()

if(VTK_FOUND)
    target_compile_definitions(MebelStudio3D_cpp PRIVATE MEBEL_HAVE_VTK)
    target_link_libraries(MebelStudio3D_cpp PRIVATE ${VTK_LIBRARIES})
    vtk_module_autoinit(TARGETS MebelStudio3D_cpp MODULES ${VTK_LIBRARIES})
endif()

if(MSVC)
    target_compile_options(MebelStudio3D_cpp PRIVATE /W4 /utf-8 /fp:precise)
else()
    # -ffp-contract=off: bez FMA, żeby wyniki liczbowe były identyczne jak w Pythonie (testy zgodności 1e-9)
    target_compile_options(MebelStudio3D_cpp PRIVATE -Wall -Wextra -Wno-missing-field-initializers -ffp-contract=off)
    if(MINGW)
        # nlohmann::json + szablony generują obiekty z ponad 65 tys. sekcji
        target_compile_options(MebelStudio3D_cpp PRIVATE -Wa,-mbig-obj)
    endif()
endif()

if(WIN32)
    target_link_libraries(MebelStudio3D_cpp PRIVATE user32 gdi32 shell32 ole32)
    if(MINGW)
        if(MEBEL_GUI_SUBSYSTEM)
            target_link_options(MebelStudio3D_cpp PRIVATE -mwindows)
        else()
            target_link_options(MebelStudio3D_cpp PRIVATE -mconsole)
        endif()
        target_compile_options(MebelStudio3D_cpp PRIVATE "$<$<COMPILE_LANGUAGE:RC>:--codepage=65001>")
    endif()
endif()

# ---------------------------------------------------------------- po zbudowaniu
# Pierwszeństwo ma native-dll/ (wynik ./dev.sh dll — świeże DLL ze źródeł), potem MEBEL_NATIVE_DIR.
# Plik poniżej 4 KB to wskaźnik Git LFS (nie prawdziwa biblioteka) — wtedy ostrzegamy.
foreach(dll MebelModel.dll MebelNative3D.dll)
    set(dll_src "")
    foreach(dir "${CMAKE_CURRENT_SOURCE_DIR}/native-dll" "${MEBEL_NATIVE_DIR}")
        if(NOT dll_src AND EXISTS "${dir}/${dll}")
            file(SIZE "${dir}/${dll}" dll_size)
            if(dll_size GREATER 4096)
                set(dll_src "${dir}/${dll}")
            else()
                message(WARNING "${dir}/${dll} ma ${dll_size} B — to wskaźnik Git LFS, nie biblioteka. Zbuduj ją: ./dev.sh dll")
            endif()
        endif()
    endforeach()
    if(dll_src)
        message(STATUS "${dll}: ${dll_src}")
        add_custom_command(TARGET MebelStudio3D_cpp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll_src}" "$<TARGET_FILE_DIR:MebelStudio3D_cpp>"
            VERBATIM)
    else()
        message(WARNING "Brak działającego ${dll}: uruchom ./dev.sh dll albo ustaw -DMEBEL_NATIVE_DIR=<katalog z DLL>")
    endif()
endforeach()

# Manifest migracji (panel „stan migracji”) leży w korzeniu repozytorium — kopiujemy obok programu.
if(EXISTS "${MEBEL_NATIVE_DIR}/MIGRACJA_CPP.json")
    add_custom_command(TARGET MebelStudio3D_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MEBEL_NATIVE_DIR}/MIGRACJA_CPP.json" "$<TARGET_FILE_DIR:MebelStudio3D_cpp>"
        VERBATIM)
endif()

if(WIN32 AND MEBEL_BUNDLE_RUNTIME)
    get_filename_component(MEBEL_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(MEBEL_TCL_LIB_DIR "${MEBEL_TCL_LIBRARY}" DIRECTORY)
    set(MEBEL_SEARCH_DIRS "${MEBEL_TOOLCHAIN_BIN}" "${MEBEL_TCL_LIB_DIR}/../bin")
    add_custom_command(TARGET MebelStudio3D_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DEXE=$<TARGET_FILE:MebelStudio3D_cpp>"
            "-DDEST=$<TARGET_FILE_DIR:MebelStudio3D_cpp>"
            "-DSEARCH_DIRS=${MEBEL_SEARCH_DIRS}"
            "-DTCL_DIR=${MEBEL_TCL_SCRIPT_DIR}"
            "-DTK_DIR=${MEBEL_TK_SCRIPT_DIR}"
            "-DTCL8_DIR=${MEBEL_TCL8_SCRIPT_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/bundle_runtime.cmake"
        VERBATIM)
endif()

# ---------------------------------------------------------------- test warstwy Tk (bez Tcl/Tk)
if(MEBEL_BUILD_TK_TESTS)
    enable_testing()
    add_executable(test_tk_mock tests/tk_mock/test_tk.cpp tests/tk_mock/tclmock.cpp tk.cpp ttk.cpp tkcore.cpp)
    # <tk.h>/<tcl.h> -> atrapa (przez -I), "tk.h" -> nagłówek projektu (przez -iquote)
    target_include_directories(test_tk_mock PRIVATE tests/tk_mock/include)
    target_compile_options(test_tk_mock PRIVATE -Wall -Wextra -iquote "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME tk_mock COMMAND test_tk_mock)
endif()
