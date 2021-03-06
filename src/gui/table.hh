#ifndef TABLE_HH_GUARD
#define TABLE_HH_GUARD

#include <iostream>
#include <stdint.h>

namespace lexer {

enum class TableTokenType {
    INVALID=231,
    TK_NAME=232,
    TK_BADINT=233,
    TK_INT=234,
    TK_TEXT=235,
    _TK_COMMENT=236,
    _WHITESPACE=237,
    _NEWLINE=238,
    TK_ASSIGN=239,
    TK_AT=240,
    TK_BANG=241,
    TK_BANGGT=242,
    TK_BANGLT=243,
    TK_BLOCKEND=244,
    TK_BLOCKSTART=245,
    TK_CHOICE=246,
    TK_COLON=247,
    TK_COMMA=248,
    TK_CONCAT=249,
    TK_DIFFERENT=250,
    TK_DIV=251,
    TK_EQUAL=252,
    TK_GREATER=253,
    TK_GREATEREQUAL=254,
    TK_ISANY=255,
    TK_ISATOM=256,
    TK_ISBOOL=257,
    TK_ISFUNC=258,
    TK_ISINT=259,
    TK_ISREL=260,
    TK_ISTEXT=261,
    TK_ISTUP=262,
    TK_LBRACKET=263,
    TK_LEFT_ARROW=264,
    TK_LESS=265,
    TK_LESSEQUAL=266,
    TK_LPAREN=267,
    TK_MINUS=268,
    TK_MUL=269,
    TK_ONE_DOT=270,
    TK_OPEXTEND=271,
    TK_PIPE=272,
    TK_PLUS=273,
    TK_PROJECT_MINUS=274,
    TK_PROJECT_PLUS=275,
    TK_QUESTION=276,
    TK_RBRACKET=277,
    TK_RIGHTARROW=278,
    TK_RPAREN=279,
    TK_SEMICOLON=280,
    TK_SET_MINUS=281,
    TK_SHARP=282,
    TK_STDBOOL=283,
    TK_STDINT=284,
    TK_STDTEXT=285,
    TK_TILDE=286,
    TK_TWO_DOTS=287,
    TK_ADD=288,
    TK_AFTER=289,
    TK_AND=290,
    TK_BEFORE=291,
    TK_CLOSE=292,
    TK_COUNT=293,
    TK_DATE=294,
    TK_DAYS=295,
    TK_END=296,
    TK_FALSE=297,
    TK_FI=298,
    TK_FUNC=299,
    TK_HAS=300,
    TK_IF=301,
    TK_IN=302,
    TK_MAX=303,
    TK_MIN=304,
    TK_MOD=305,
    TK_MULT=306,
    TK_NOT=307,
    TK_ONE=308,
    TK_OPEN=309,
    TK_OR=310,
    TK_REL=311,
    TK_SYSTEM=312,
    TK_TODAY=313,
    TK_TRUE=314,
    TK_TUP=315,
    TK_TYPE_ANY=316,
    TK_TYPE_ATOM=317,
    TK_TYPE_BOOL=318,
    TK_TYPE_FUNC=319,
    TK_TYPE_INT=320,
    TK_TYPE_REL=321,
    TK_TYPE_TEXT=322,
    TK_TYPE_TUP=323,
    TK_VAL=324,
    TK_WRITE=325,
    TK_UNSET=326,
    TK_ZERO=327,
    TK_PRINT=328};
extern int table[][256];
const int initialState=0;
} // end namespace lexer

#endif // TABLE_HH_GUARD
