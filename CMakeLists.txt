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
        "assets/404.html"
    REQUIRES 
        "littlefs"
        "mbedtls"
        "esp_timer"
        "my-sysinfo"
        "my-asynctcp"
)