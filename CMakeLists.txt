cmake_minimum_required(VERSION 3.12)

project(Tutorial VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(fmt)
add_executable( 
    main
    main.cpp 
    terp.h terp.cpp
    result.h result_message.h
    hex_formatter.h hex_formatter.cpp
    
)


target_link_libraries(main fmt)

