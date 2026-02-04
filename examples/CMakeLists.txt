cmake_minimum_required(VERSION 3.20)
project(examples)

add_executable(example)

target_sources(example
  PRIVATE
    example.cpp
)
