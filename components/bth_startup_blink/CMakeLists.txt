idf_component_register(
    SRCS "src/startup_blink.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bth_core
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
