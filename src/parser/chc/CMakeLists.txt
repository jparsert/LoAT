target_sources(${EXECUTABLE}
    PRIVATE
        CHCParseVisitor.cpp
        CHCParseVisitor.h
        CHCLexer.cpp
        CHCLexer.h
        CHCListener.cpp
        CHCListener.h
        CHCParser.cpp
        CHCParser.h
        CHCVisitor.cpp
        CHCVisitor.h
        chcparser.cpp
        chcparser.hpp
)

target_include_directories(${EXECUTABLE} PRIVATE ".")
