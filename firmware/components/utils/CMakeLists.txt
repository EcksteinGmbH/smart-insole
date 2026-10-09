# idf_component_register(SRCS "*.c"
#                     INCLUDE_DIRS "include"
#                     REQUIRES driver esp_timer)


file(GLOB_RECURSE srcs "*.c")

set(includes
        "include"
        )


idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${includes}"
                       REQUIRES driver esp_timer)