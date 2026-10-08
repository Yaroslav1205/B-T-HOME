idf_component_register(
    SRCS "src/battery.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bth_core bth_mains bth_dc
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
