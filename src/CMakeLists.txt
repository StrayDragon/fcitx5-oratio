add_library(oratio SHARED oratio.cpp)
target_link_libraries(oratio PRIVATE Fcitx5::Core Fcitx5::Config)
install(TARGETS oratio DESTINATION "${FCITX_INSTALL_ADDONDIR}")

configure_file(oratio-addon.conf.in oratio-addon-configured.conf @ONLY)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/oratio-addon-configured.conf"
    RENAME "oratio.conf"
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
    COMPONENT config
)

configure_file(oratio.conf.in oratio-config-configured.conf @ONLY)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/oratio-config-configured.conf"
    RENAME "oratio.conf"
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/conf"
    COMPONENT config
)