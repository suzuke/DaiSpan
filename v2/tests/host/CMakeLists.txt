cmake_minimum_required(VERSION 3.16)
project(daispan_host_tests C)

enable_testing()

add_executable(test_dns_responder
    test_dns_responder.c
    ../../main/wifi/dns_responder.c
)
target_include_directories(test_dns_responder PRIVATE ../../main)
target_compile_options(test_dns_responder PRIVATE -Wall -Wextra -Werror)

add_test(NAME dns_responder COMMAND test_dns_responder)
