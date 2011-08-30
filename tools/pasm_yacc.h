
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton interface for Bison's Yacc-like parsers in C
   
      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.
   
   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     INTEGER = 258,
     VARIABLE = 259,
     WHILE = 260,
     IF = 261,
     PUTCH = 262,
     PUTD = 263,
     EXIT = 264,
     SYSTEM = 265,
     SPRINT = 266,
     STRING = 267,
     CR = 268,
     MORSE = 269,
     TIME = 270,
     ARGD = 271,
     ARGCH = 272,
     SET_TIME = 273,
     SET_DATE = 274,
     GETD = 275,
     GETCH = 276,
     CLEAR = 277,
     FPUTCH = 278,
     FFLUSH = 279,
     IFX = 280,
     ELSE = 281,
     NE = 282,
     EQ = 283,
     LE = 284,
     GE = 285,
     UMINUS = 286
   };
#endif
/* Tokens.  */
#define INTEGER 258
#define VARIABLE 259
#define WHILE 260
#define IF 261
#define PUTCH 262
#define PUTD 263
#define EXIT 264
#define SYSTEM 265
#define SPRINT 266
#define STRING 267
#define CR 268
#define MORSE 269
#define TIME 270
#define ARGD 271
#define ARGCH 272
#define SET_TIME 273
#define SET_DATE 274
#define GETD 275
#define GETCH 276
#define CLEAR 277
#define FPUTCH 278
#define FFLUSH 279
#define IFX 280
#define ELSE 281
#define NE 282
#define EQ 283
#define LE 284
#define GE 285
#define UMINUS 286




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 1676 of yacc.c  */
#line 50 "pasm_yacc.y"

    int iValue;                 /* integer value */
    char sIndex;                /* symbol table index */
    nodeType *nPtr;             /* node pointer */



/* Line 1676 of yacc.c  */
#line 122 "pasm_yacc.h"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

extern YYSTYPE yylval;

