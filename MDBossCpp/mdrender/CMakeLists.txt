find_package(md4c CONFIG REQUIRED)

add_library(mdrender STATIC
    src/MdRender.cpp
    src/Text.cpp
    src/FrontMatter.cpp
    src/Outline.cpp
    src/Preprocess.cpp
    src/Sanitize.cpp
    src/HtmlRenderer.cpp
)

target_include_directories(mdrender
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Only the core parser is used; md4c-html would render HTML5 void elements,
# while markdown-it (and therefore this port) emits XHTML ones.
target_link_libraries(mdrender PRIVATE md4c::md4c)
