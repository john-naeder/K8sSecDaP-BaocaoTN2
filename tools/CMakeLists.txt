add_executable(trace_algorithms trace_algorithms.cpp)
target_link_libraries(trace_algorithms PRIVATE dsa)

add_subdirectory(portscan-pipeline)

# Convenience target: regenerate trace outputs used by the report.
add_custom_target(trace_outputs
    COMMAND $<TARGET_FILE:trace_algorithms> cms    > ${CMAKE_SOURCE_DIR}/output/trace_cms.txt
    COMMAND $<TARGET_FILE:trace_algorithms> misra  > ${CMAKE_SOURCE_DIR}/output/trace_misra_gries.txt
    COMMAND $<TARGET_FILE:trace_algorithms> compare > ${CMAKE_SOURCE_DIR}/output/trace_comparison.txt
    DEPENDS trace_algorithms
    COMMENT "Regenerating algorithm traces into output/"
)
