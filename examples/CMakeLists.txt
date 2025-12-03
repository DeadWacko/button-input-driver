add_executable(buttons main.c)

target_link_libraries(buttons PRIVATE buttonlib pico_stdlib)

pico_set_program_name(buttons "buttons")
pico_set_program_version(buttons "0.1")

pico_enable_stdio_uart(buttons 0)
pico_enable_stdio_usb(buttons 1)

pico_add_extra_outputs(buttons)
