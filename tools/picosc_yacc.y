%{
#include "pasm.h"
#include "../piclang.h"
#include "utils.h"
#include "../page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <getopt.h>
#include <errno.h>

struct compiled_code *the_code;
struct compiled_code *the_code_end;
struct compiled_code *the_strings;
struct compiled_code *the_strings_end;
struct subroutine_map *subroutines;

extern struct assembly_map opcodes[];

char **string_list;
size_t num_strings;
picos_size_t FS_BUFFER_SIZE;

extern picos_size_t label_counter;
int break_to_label, continue_to_label;
idNodeType *variable_list = NULL;

/* prototypes */
void freeNode(nodeType *p);
int ex(nodeType *p);
int yylex(void);
 FILE *assembly_file;
 FILE *lst_file;
void yyerror(char *s);
 int resolve_string(const char *str, int *is_new);
 int resolve_variable(const char *name);// Looks up a variable and retrieves its page memory index. If the variable does not yet exist, it will be added to the list.
 extern char *yytext;
 extern char *last_string;
 nodeType* store_string(const char *);
  
%}

%union {
    int iValue;                 /* integer value */
    idNodeType variable;          /* symbol table index */
    nodeType *nPtr;             /* node pointer */
};

%token <iValue> INTEGER FUNCT
%token <variable> VARIABLE
%type <nPtr> stmt expr stmt_list STRING SUBROUTINE PREPROC_KEYWORD

%token WHILE BREAK CONTINUE IF CALL SUBROUTINE
%token STRING RETURN DEFINE EXIT PREPROC_KEYWORD CONST
%token PASM_CR PASM_POP ARGV ARGC ERRNO FIN FEOF STATEMENT_DELIM

%nonassoc IFX
%nonassoc ELSE

%left BSL BSR GE LE EQ NE '>' '<'
%left '+' '-'
%left '*' '/'
%left '%'
%nonassoc UMINUS

%locations

%%

program:
        function                { YYACCEPT; }
        ;

function: 
         function stmt         { ex($2); freeNode($2); }
        | /* NULL */
        ;

stmt: 
';'                            { $$ = opr(PASM_STATEMENT_DELIM, 2, NULL, NULL); }
        | RETURN stmt                     { $$ = opr(PICLANG_RETURN,1,$2);}
        | CALL SUBROUTINE ';'            { $$ = opr(PICLANG_CALL,1,$2); }
        | DEFINE SUBROUTINE  stmt       {  $$ = opr(PASM_DEFINE,2,$2,$3);}
        | PASM_CR '(' ')' ';'                 { $$ = opr(PICLANG_PRINTL,1,con(0xa));}
        | expr ';'                       { $$ = $1; }
        | CONST VARIABLE '=' expr ';'    { $$ = opr(PASM_INITIALIZATION, 2, const_id($2,true), $4); }
        | VARIABLE '=' expr ';'          { $$ = opr(PICLANG_POP, 2, id($1), $3); }
        | WHILE '(' expr ')' stmt        { $$ = opr(PASM_WHILE, 2, $3, $5); }
        | BREAK ';'                      { $$ = opr(PASM_BREAK, 0); }
        | CONTINUE ';'                   { $$ = opr(PASM_CONTINUE, 0); }
        | IF '(' expr ')' stmt %prec IFX { $$ = opr(PASM_IF, 2, $3, $5); }
        | IF '(' expr ')' stmt ELSE stmt { $$ = opr(PASM_IF, 3, $3, $5, $7); }
        | '{' stmt_list '}'              { $$ = $2; }
        | PREPROC_KEYWORD stmt {preprocess($1->str.string,$2); $$ = opr(PASM_STATEMENT_DELIM, 0); }
        | EXIT {YYACCEPT;}
        ;



stmt_list:
          stmt                  { $$ = $1; }
        | stmt_list stmt        { $$ = opr(PASM_STATEMENT_DELIM, 2, $1, $2); }
        ;

expr:
          INTEGER               { $$ = con($1); }
        | VARIABLE              { $$ = id($1); }
        | STRING                { $$ = con(handle_string($1->str.string)); }
        | ARGC                  { $$ = opr(PICLANG_ARGC,0); }
        | ERRNO                  { $$ = opr(PICLANG_ERRNO,0); }
        | FIN                   { $$ = con(ARG_SIZE); }
        | FEOF                   { $$ = con(((picos_size_t)(-1))); }
        | ARGV '[' expr ']'     { $$ = opr(PICLANG_ARGV,1,$3); }
        | PASM_POP '(' ')'      { $$ = opr(PICLANG_POP,0); }
        | FUNCT '(' expr ')'    { $$ = opr($1,1,$3); }
        | FUNCT '(' INTEGER ',' SUBROUTINE ')' { $$ = opr($1,2,con($3),$5); }
        | FUNCT '(' expr ',' expr  ')'    { $$ = opr($1,2,$3,$5); }
        | FUNCT '(' expr ',' expr ',' expr ')'    { $$ = opr($1,3,$3,$5,$7); }
        | FUNCT '(' ')'         { $$ = opr($1,0); }
        | expr '[' expr ']' { $$ = opr(PICLANG_DEREF,2, $1,$3); }
        | '-' expr %prec UMINUS { $$ = opr(PICLANG_UMINUS, 1, $2); }
        | expr '+' expr         { $$ = opr(PICLANG_ADD, 2, $1, $3); }
        | expr '-' expr         { $$ = opr(PICLANG_SUB, 2, $1, $3); }
        | expr '*' expr         { $$ = opr(PICLANG_MULT, 2, $1, $3); }
        | expr '/' expr         { $$ = opr(PICLANG_DIV, 2, $1, $3); }
        | expr '<' expr         { $$ = opr(PICLANG_COMPLT, 2, $1, $3); }
        | expr '>' expr         { $$ = opr(PICLANG_COMPGT, 2, $1, $3); }
        | expr '%' expr         { $$ = opr(PICLANG_MOD, 2, $1, $3); }
        | expr '&' expr         { $$ = opr(PICLANG_AND, 2, $1, $3); }
        | expr '|' expr         { $$ = opr(PICLANG_OR, 2, $1, $3); }
        | '~' expr              { $$ = opr(PICLANG_NOT, 1, $2); }
        | expr BSL expr          { $$ = opr(PICLANG_BSL, 2, $1, $3); }
        | expr BSR expr          { $$ = opr(PICLANG_BSR, 2, $1, $3); }
        | expr NE expr          { $$ = opr(PICLANG_COMPNE, 2, $1, $3); }
        | expr EQ expr          { $$ = opr(PICLANG_COMPEQ, 2, $1, $3); }
        | '(' expr ')'          { $$ = $2; }
        ;

%%



static const char short_options[] = "a:b:e:hl:n:o:";
enum OPTION_INDICES{OUTPUT_HEX};
static struct option long_options[] =
             {
	       {"help",0,NULL,'h'},
               {"hex", 1,NULL, OUTPUT_HEX},
	       {"asm", 1,NULL, 'a'},
	       {"eeprom",1,NULL, 'e'},
	       {"binary",1,NULL,'o'},
	       {"list",1,NULL,'l'},
	       {"linkage",1,NULL,'n'},
	       {"buffer_size",1,NULL,'b'},
               {0, 0, 0, 0}
             };

void print_help()
{
  printf("\n");
  printf("picosc -- Piclang compiler.\n");
  printf("Copyright 2011 David Coss, PhD\n");
  printf("-------------------------------\n");
  printf("Compiles piclang programs for use with the Pic Operating System.\n");
  printf("Note: If no source file is provided, the compiler will act as \n\tan interpreter.\n");
  printf("\n");
  printf("Usage: picosc [options] [source code]\n\n");
  printf("Options:\n");
  printf("--help, -h :\t\t Displays this dialog.\n");
  printf("--asm,-a <file> :\t Outputs the assembly to the specified file.\n");
  printf("--hex <file>    :\t Outputs Intel Hex to the specified file.\n");
  printf("--eeprom, -e <file> :\t Outputs \"__EEPROM_DATA(...)\" code for use\n");
  printf("                     \t with the Hi Tech C Compiler.\n");
  printf("--binary, -o <file> :\t Outputs a binary file containing the compiled program.\n");
  printf("--list, -l <file> :\t Outputs a list of program addresses (PC values) for each assembly entry.\n");
  printf("--linkage, -n <file> :\t Outputs a list of linkage for jumps and calls.\n");
  printf("--block_size, -b <INT> :\t Sets the size of block of the target PICFS (Default: 128)");
}



int main(int argc, char **argv) 
{
  char hex_buffer[45];
  FILE *hex_file = NULL, *eeprom_file = NULL, *binary_file = NULL;
  FILE *lnk_file = NULL;
  char opt;
  int opt_index;
  picos_size_t piclang_bitmap = 0;
  struct compiled_code *curr_code = NULL;

  assembly_file = NULL;
  the_code_end = the_code = NULL;
  the_strings = the_strings = NULL;
  string_list = NULL;num_strings = 0;
  variable_list = NULL;
  subroutines = NULL;
  break_to_label = -1;
  continue_to_label = -1;
  FS_BUFFER_SIZE = 128;
  
  while(true)
    {    
      opt = getopt_long(argc,argv,short_options,long_options,&opt_index);
      if(opt == -1)
	break;
      
      switch(opt)
	{
	case OUTPUT_HEX:
	  hex_file = fopen(optarg,"w");
	  if(hex_file == NULL)
	    hex_file = stdout;
	  break;
	case 'b':
	  if(sscanf(optarg,"%hu",&FS_BUFFER_SIZE) != 1)
	    {
	      fprintf(stderr,"Could not read buffers size: %s\n",optarg);
	      exit(-1);
	    }
	  break;
	case 'a':
	  assembly_file = fopen(optarg,"w");
	  if(assembly_file == NULL)
	    assembly_file = stdout;
	  break;
	case 'l':
	  lst_file = fopen(optarg,"w");
	  if(lst_file == NULL)
	    lst_file = stdout;
	  break;
	case 'n':
	  lnk_file = fopen(optarg,"w");
	  if(lnk_file == NULL)
	    lnk_file = stdout;
	  break;
	case 'o':
	  binary_file = fopen(optarg,"w");
	  if(binary_file == NULL)
	    {
	      fprintf(stderr,"Could not open %s for writing.\n",optarg);
	      exit(ENOENT);
	    }
	  break;
	case 'e':
	  eeprom_file = fopen(optarg,"w");
	  if(eeprom_file == NULL)
	    eeprom_file = stdout;
	  break;
	case 'h':
	  print_help();
	  return 0;
	default:
	  fprintf(stderr,"ERROR - Unknown flag %c\n",opt);
	  print_help();
	  return -1;
	}
    }

  if(optind < argc)
    {
      FILE *input = fopen(argv[optind++],"r");
      extern FILE *yyin;
      if(input != NULL)
	yyin = input;
    }
  else
    printf("Welcome to the piclang compiler.\n");
  
  yyparse();

  if(hex_file == stdout)
    printf("Here comes your code.\nThank you come again.\nCODE:\n");
  pasm_compile(eeprom_file,hex_file,&the_code,the_strings,&piclang_bitmap,count_variables());

  if(binary_file != NULL)
    {
      curr_code = the_code;
      while(curr_code != NULL)
	{
	  if(curr_code->type != typeStr)
	    write_val_for_pic(binary_file,curr_code->val);
	  else
	    fprintf(binary_file,"%c",(char)curr_code->val);
	  curr_code = curr_code->next;
	}
    }

  create_lst_file(lst_file, the_code, the_strings);
  
  create_lnk_file(lnk_file, the_code);
  
  FreeCode(the_code);
  return 0;
}
