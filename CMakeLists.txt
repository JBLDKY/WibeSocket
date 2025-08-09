add_library(ultraws SHARED src/ultraws.c src/parser.c src/event_loop.c)
target_include_directories(ultraws PUBLIC include)
set_target_properties(ultraws PROPERTIES POSITION_INDEPENDENT_CODE ON)
