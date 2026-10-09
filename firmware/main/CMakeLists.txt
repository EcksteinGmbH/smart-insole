# idf_component_register(SRCS "ble_nim.c" "ble_v2.c" "loops.c" "protocol.c" "main.c"
#                     INCLUDE_DIRS ".")

file(GLOB_RECURSE srcs "*.c" "src/*.c")

set(includes
        "."
        "include"
        )


idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${includes}")