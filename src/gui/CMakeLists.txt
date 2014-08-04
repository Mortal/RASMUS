include(${QT_USE_FILE})

include_directories(${QT_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
QT4_WRAP_UI(UISrs main.ui)
add_executable(rasmus ${UISrs} console.hh main.cc interpreter.cc interpreter.hh console.cc)

target_link_libraries(rasmus frontend ${QT_LIBRARIES} -pthread stdlib)
set_target_properties(rasmus PROPERTIES AUTOMOC TRUE)
set_target_properties(rasmus PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../../)
