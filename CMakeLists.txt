set(HTML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(HTML_FILES
    "404.html"
)
foreach(html_file ${HTML_FILES})
    set(html_path "${HTML_DIR}/${html_file}")
    set(gz_path "${HTML_DIR}/${html_file}.gz")

    if(NOT EXISTS "${gz_path}")
        execute_process(
            COMMAND gzip -9 -c ${html_path}
            OUTPUT_FILE ${gz_path}
        )
    endif()
endforeach()


file(GLOB SOURCE_CODE
    "src/*.cc"
    "src/handler/*.cc"
    "src/header/*.cc"
    "src/parameter/*.cc"
    "src/request/*.cc"
    "src/response/*.cc"
    "src/rewrite/*.cc"
    "src/socket/*.cc"
)




idf_component_register(
    SRCS 
        ${SOURCE_CODE}
    INCLUDE_DIRS 
        "include"
    EMBED_TXTFILES
        "assets/404.html.gz"
    REQUIRES 
        "littlefs"
        "mbedtls"
        "esp_timer"
        "my-sysinfo"
        "my-asynctcp"
)
