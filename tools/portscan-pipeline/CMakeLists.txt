add_executable(portscan-pipeline
    main.cpp
    parser.cpp
    detector.cpp
)
target_include_directories(portscan-pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(portscan-pipeline PRIVATE dsa)
