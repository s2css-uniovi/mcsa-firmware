idf_component_register(SRCS "mqtt-task.c" "sending_task.c" "sampling_task.c" "buffer.c" "main.c" "adc.c"
                    INCLUDE_DIRS "")

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
