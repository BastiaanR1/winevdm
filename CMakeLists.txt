cmake_minimum_required(VERSION 3.10.2)
#-Dinline=__inline
project(winevdm)
set(ASM_TOOL_DIR )
# prevent wine error
if (NOT(MSVC))
    string(APPEND CMAKE_CXX_FLAGS " -Wno-narrowing ")
else()
    add_definitions(-DUSE_COMPILER_EXCEPTIONS -Dinline=__inline)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
macro(def_file_build32 file)
    get_filename_component(name_we ${file} NAME_WE)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def
    DEPENDS ${file}.spec convspec
    COMMAND convspec ${CMAKE_CURRENT_LIST_DIR}/${file}.spec -DEF -32 > ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def)
    add_custom_target(generate_def_${name_we} DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def)
endmacro()
macro(def_file_build file)
    get_filename_component(name_we ${file} NAME_WE)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def
    DEPENDS ${file}.spec convspec
    COMMAND convspec ${CMAKE_CURRENT_LIST_DIR}/${file}.spec -DEF > ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def)
    add_custom_target(generate_def_${name_we} DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name_we}.def)
endmacro()
macro(spec_build file modname)
    string(TOUPPER modname modname)
    get_filename_component(name_we ${file} NAME_WE)
    cmake_parse_arguments(SPEC_BUILD "" "" "ARG" ${ARGN})
    add_custom_command(OUTPUT ${file}.obj ${file}.asm
    DEPENDS ${file}.spec convspec
    COMMAND convspec ${CMAKE_CURRENT_LIST_DIR}/${file}.spec ${SPEC_BUILD_ARG} ${modname} > ${file}.asm && ${ASM_TOOL_DIR}as --32 -o ${file}.obj ${file}.asm)
    add_custom_target(generate_asm_${name_we} DEPENDS ${file}.obj)
    if (MSVC)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
    endif()
    def_file_build(${file})
    add_dependencies(${name_we} generate_asm_${name_we})
    add_dependencies(${name_we} generate_def_${name_we})
    set_target_properties(${name_we} PROPERTIES PREFIX "")
endmacro()
macro(widl_build file_idl file_c file_h)
    get_filename_component(name_we ${file_idl} NAME_WE)
    add_custom_command(OUTPUT ${file_c} ${file_h}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file_idl} widl
    COMMAND $<TARGET_FILE_DIR:widl>/widl.exe ${CMAKE_CURRENT_SOURCE_DIR}/${file_idl} -o ${file_c} && $<TARGET_FILE_DIR:widl>/widl.exe ${CMAKE_CURRENT_SOURCE_DIR}/${file_idl} -o ${file_h}
WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_custom_target(generate_ifs_${name_we} DEPENDS ${file_c} ${file_h})
endmacro()

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES(WinHvPlatform.h HAVE_WINHVPLATFORM_H)

include_directories(BEFORE wow32)
find_package(BISON)
find_package(FLEX)

add_subdirectory(wine)
add_subdirectory(convspec)
add_subdirectory(winecrt0)
add_subdirectory(wow32)
add_subdirectory(krnl386)
add_subdirectory(user)
add_subdirectory(gdi)
add_subdirectory(win87em)
add_subdirectory(shell)
add_subdirectory(vm86)
add_subdirectory(avifile)
add_subdirectory(comm)
add_subdirectory(commctrl)
add_subdirectory(commdlg)
add_subdirectory(compobj)
add_subdirectory(ctl3d)
add_subdirectory(ctl3dv2)
add_subdirectory(ddeml)
add_subdirectory(dispdib)
add_subdirectory(display)
add_subdirectory(haxmvm)
add_subdirectory(keyboard)
add_subdirectory(lzexpand)
add_subdirectory(mmsystem)
add_subdirectory(mouse)
add_subdirectory(msacm)
add_subdirectory(msvideo)
add_subdirectory(nddeapi)
add_subdirectory(ole2)
add_subdirectory(ole2conv)
add_subdirectory(ole2disp)
add_subdirectory(ole2nls)
add_subdirectory(ole2prox)
add_subdirectory(ole2thk)
add_subdirectory(olecli)
add_subdirectory(olesvr)
add_subdirectory(regedit)
add_subdirectory(rmpatch)
add_subdirectory(sound)
add_subdirectory(storage)
add_subdirectory(system)
add_subdirectory(timer)
add_subdirectory(toolhelp)
add_subdirectory(typelib)
add_subdirectory(ver)
add_subdirectory(widl)
add_subdirectory(wifeman)
add_subdirectory(wing)
add_subdirectory(winhlp32)
add_subdirectory(winnls)
add_subdirectory(winoldap)
add_subdirectory(winsock)
add_subdirectory(winspool)
add_subdirectory(wpp)
add_subdirectory(otvdm)
add_subdirectory(gvm)
add_subdirectory(ntvdm)
if (HAVE_WINHVPLATFORM_H)
  add_subdirectory(whpxvm)
endif()
