cmake_minimum_required(VERSION 3.10)

set(COMPONENT_SRCS
    "vol_ctrl.cpp"
    "device_state.cpp"
    "display.cpp"
    "network.cpp"
    "utils.cpp"
)

set(COMPONENT_REQUIRES
    "esphome"
    "esphome-core"
    "esphome-components-wifi"
    "esphome-components-spi"
)

set(COMPONENT_PRIV_INCLUDEDIRS ".")

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${COMPONENT_REQUIRES}
)
