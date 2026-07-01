idf_component_register(
    SRCS "main.c" "web_server.c" "uart_fft.c" "stm32_sim.c"
    INCLUDE_DIRS "."
    EMBED_FILES "../html/root.html"
               "../html/main.js"
)
