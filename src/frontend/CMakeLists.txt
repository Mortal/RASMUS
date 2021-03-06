add_custom_command(
  OUTPUT AST.hh visitor.hh nodetype.hh
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ASTGen.py ${CMAKE_CURRENT_SOURCE_DIR}/AST.txt 
  MAIN_DEPENDENCY AST.txt 
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/ASTGen.py)

add_library(frontend
  AST.hh
  nodetype.hh
  visitor.hh

  ASTBase.hh
  callback.cc
  callback.hh
  charRanges.cc
  charRanges.hh
  code.hh
  error.cc
  error.hh
  firstParse.cc
  firstParse.hh
  interpreter.cc
  interpreter.hh
  lexer.cc
  lexer.hh
  llvmCodeGen.cc
  llvmCodeGen.hh
  parser.cc
  parser.hh
  astPrinter.hh
  astPrinter.cc
  tokenizer.hh
  tokenizer.cc
)

target_link_libraries(frontend -L${LLVM_LIBRARY_DIR} ${LLVM_MODULE_LIBS} ${LLVM_LFLAGS} -lm stdlib -ldl -lz -lpthread -lncurses)
