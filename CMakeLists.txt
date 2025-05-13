cmake_minimum_required(VERSION 3.30)
project(tcp DESCRIPTION "TCP client_lib and server_lib" LANGUAGES C)

add_library(server_lib lib/server/private/server.c)
target_include_directories(server_lib PUBLIC lib/server/public)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(server_lib PUBLIC 
    -Wall -Wextra -Werror -Wpedantic -fsanitize=address
  )
  target_link_options(server_lib PUBLIC -fsanitize=address)
endif()

add_executable(server app/main.c)
target_link_libraries(server PRIVATE server_lib)

add_library(client_lib lib/client/private/client.c)
target_include_directories(client_lib INTERFACE lib/client/public)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(client_lib PUBLIC 
    -Wall -Wextra -Werror -Wpedantic -fsanitize=address
  )
  target_link_options(client_lib PUBLIC -fsanitize=address)
endif()

add_executable(client app/main.c)
target_link_libraries(client PRIVATE client_lib)
target_compile_definitions(client PRIVATE CLIENT)