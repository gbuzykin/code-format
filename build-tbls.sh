#!/bin/bash -e
LEXEGEN_PATH=${LEXEGEN_PATH:-lexegen}
$LEXEGEN_PATH src/cpp.lex --header-file=src/lex_defs.h --outfile=src/lex_analyzer.inl
