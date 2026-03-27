if(WIN32)
    set(RootDir "@RootDir@")
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/windows.xml.in
        ${CMAKE_BINARY_DIR}/installer/config/windows.xml
    )
    file(COPY
        ${CMAKE_CURRENT_LIST_DIR}/config/controlscript.js
        ${CMAKE_CURRENT_LIST_DIR}/config/frkn.ico
        ${CMAKE_CURRENT_LIST_DIR}/config/frkn.png
        DESTINATION ${CMAKE_BINARY_DIR}/installer/config/
    )
elseif(LINUX)
    set(ApplicationsDir "@ApplicationsDir@")
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/linux.xml.in
        ${CMAKE_BINARY_DIR}/installer/config/linux.xml
    )
    
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/FRKN.desktop.in
        ${CMAKE_BINARY_DIR}/../AppDir/FRKN.desktop
    )
endif()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/packages/org.frkn.package/meta/package.xml.in
    ${CMAKE_BINARY_DIR}/installer/packages/org.frkn.package/meta/package.xml
)

file(COPY ${CMAKE_CURRENT_LIST_DIR}/packages/org.frkn.package/meta/componentscript.js
     DESTINATION ${CMAKE_BINARY_DIR}/installer/packages/org.frkn.package/meta/)
