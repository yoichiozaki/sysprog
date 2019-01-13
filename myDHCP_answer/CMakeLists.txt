cmake_minimum_required(VERSION 3.13)
project(myDHCP_answer C)

set(CMAKE_C_STANDARD 99)

add_executable(myDHCP_answer main.c mydhcp.h mydhcpc.c mydhcps.c)