%{
/**
 * PICOS, PIC operating system.
 * Author: David Coss, PhD
 * Date: 20 June 2012
 * License: GNU Public License version 3.0 (see http://www.gnu.org)
 *
 * picosc grammar and main function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>

#include "picos/tools/pasm.h"
#include "picos/piclang.h"
#include "picos/utils.h"
#include "picos/page.h"

extern picos_size_t label_counter;
int break_to_label, continue_to_label, switch_end_label;
struct subroutine_map *global_subroutines = NULL, *g_curr_subroutine = NULL, *string_handler = NULL;
struct subroutine_map *global_subroutines_GLOBALS;
extern struct assembly_map opcodes[];

char **string_list;
size_t num_strings;
picos_size_t FS_BUFFER_SIZE;

/* prototypes */
void freeNode(nodeType *p);
int ex(nodeType *p);
int yylex(void);
 FILE *assembly_file;
 FILE *lst_file = NULL;
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
%type <nPtr> stmt expr stmt_list switch_block STRING SUBROUTINE PREPROC_KEYWORD

%token WHILE BREAK CONTINUE IF CALL SUBROUTINE
%token STRING RETURN DEFINE EXIT PREPROC_KEYWORD CONST
%token PASM_CR PASM_POP ARGV ARGC ERRNO FIN FEOF STATEMENT_DELIM
%token DEFAULT CASE SWITCH

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
        | SWITCH '(' expr ')' '{' switch_block '}' { $$ = opr(PASM_SWITCH, 2, $3, $6); }
        | PREPROC_KEYWORD stmt {preprocess($1->str.string,$2); $$ = opr(PASM_STATEMENT_DELIM, 0); }
        | EXIT {YYACCEPT;}
        ;

switch_block:
    switch_block CASE expr ':' stmt { $$ = opr(PASM_STATEMENT_DELIM,2, $1, opr(PASM_CASE, 2, $3, $5)); }
  | switch_block DEFAULT ':' stmt { $$ = opr(PASM_STATEMENT_DELIM,2, $1, $4); }
  | CASE expr ':' stmt { $$ = opr(PASM_CASE, 2, $2, $4); }
  | DEFAULT ':' stmt { $$ = $3; }
  ;

stmt_list:
          stmt                  { $$ = $1; }
        | stmt_list stmt        { $$ = opr(PASM_STATEMENT_DELIM, 2, $1, $2); }
        ;

expr:
          INTEGER               { $$ = con($1); }
        | VARIABLE              { $$ = id($1); }
        | STRING                { $$ = full_con(handle_string($1->str.string),REL_STRING); }
        | ARGC                  { $$ = opr(PICLANG_ARGC,0); }
        | ERRNO                  { $$ = opr(PICLANG_ERRNO,0); }
        | FIN                   { $$ = con(ARG_SIZE); }
        | FEOF                   { $$ = con(((picos_size_t)(-1))); }
        | ARGV '[' expr ']'     { $$ = opr(PICLANG_ARGV,1,$3); }
        | PASM_POP '(' ')'      { $$ = opr(PICLANG_POP,0); }
        | FUNCT '(' expr ')'    { $$ = opr($1,1,$3); }
        | FUNCT '(' expr ',' SUBROUTINE ')' { $$ = opr($1,2,$3,$5); }
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

void load_rc(char *keyword, char *arg)
{
  size_t len,idx;
  int have_arg, iarg;

  if(keyword == NULL || strlen(keyword) == 0)
    return;
  
  have_arg = (arg != NULL) & strlen(arg);

  len = strlen(keyword);idx = 0;
  for(;idx < len;idx++)
    {
      if(keyword[idx] == ' ')
	{
	  keyword[idx] = '\0';
	  break;
	}
      keyword[idx] = tolower(keyword[idx]);
    }
  
  if(have_arg)
    {
      len = strlen(arg);idx = 0;
      for(;idx < len;idx++)
	{
	  if(arg[idx] == ' ')
	    {
	      arg[idx] = '\0';
	      break;
	    }
	  arg[idx] = tolower(arg[idx]);
	}
    }

  if(strcmp(keyword,"block_size") == 0)
    {
      if(have_arg)
	{
	  if(sscanf(arg,"%d",&iarg) != 1)
	    fprintf(stderr,"Invalid block size \"%s\"\n",arg);
	  else
	    FS_BUFFER_SIZE = (picos_size_t)iarg;
	}
      else
	fprintf(stderr,"No argument for BLOCK_SIZE\n");
    }
}

static const char short_options[] = "a:b:ce:hl:n:o:v";
enum OPTION_INDICES{OUTPUT_HEX,OUTPUT_LIST,OUTPUT_LINKAGE};
static struct option long_options[] =
             {
	       {"help",0,NULL,'h'},
               {"hex", 1,NULL, OUTPUT_HEX},
	       {"asm", 1,NULL, 'a'},
	       {"binary",1,NULL,'o'},
	       {"compile",0,NULL,'c'},
	       {"eeprom",1,NULL, 'e'},
	       {"lib",1,NULL,'l'},
	       {"linkage",1,NULL,OUTPUT_LINKAGE},
	       {"list",1,NULL,OUTPUT_LIST},
	       {"buffer_size",1,NULL,'b'},
	       {"version",0,NULL,'v'},
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
  printf("--binary, -o <file> :\t Outputs a binary file containing the compiled program.\n");
  printf("--buffer_size, -b <INT> :\t Sets the size of block of the target PICFS (Default: 128)\n");
  printf("--compile, -c :\t Compile or assemble the source, but do not produce executable code.\n"); 
  printf("--eeprom, -e <file> :\t Outputs \"__EEPROM_DATA(...)\" code for use\n");
  printf("                     \t with the Hi Tech C Compiler.\n");
  printf("--hex <file>    :\t Outputs Intel Hex to the specified file.\n");
  printf("--lib, -l <file> :\t Link agains pre-compiled library (NOT YET IMPLEMENTED)\n");
  printf("--linkage <file> :\t Outputs a list of linkage for jumps and calls.\n");
  printf("--list <file> :\t Outputs a list of program addresses (PC values) for each assembly entry.\n");

}



int main(int argc, char **argv) 
{
  char hex_buffer[45];
  FILE *hex_file = NULL, *eeprom_file = NULL, *binary_file = NULL;
  FILE *lnk_file = NULL;
  char opt;
  int compile_only = false;
  int opt_index;
  picos_size_t piclang_bitmap = 0;
  struct compiled_code *curr_code = NULL, *finished_code = NULL;

  assembly_file = NULL;
  break_to_label = -1;
  continue_to_label = -1;
  switch_end_label = -1;
  FS_BUFFER_SIZE = 128;

  check_load_rc();
  
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
	case 'c':
	  compile_only = true;
	  break;
	case 'a':
	  assembly_file = fopen(optarg,"w");
	  if(assembly_file == NULL)
	    assembly_file = stdout;
	  break;
	case 'l':
	  {
	    FILE *libfile = fopen(optarg,"rb");
	    struct piclib_object* libobj = NULL;
	    
	    if(libfile == NULL)
	      {
		fprintf(stderr,"Could not open library file \"%s\"\n",optarg);
		if(errno != 0)
		  {
		    fprintf(stderr,"Reason: %s\n",strerror(errno));
		    exit(errno);
		  }
		exit(1);
	      }
	    libobj = piclib_load(libfile);
	    if(libobj == NULL)
	      {
		fprintf(stderr, "Error loading library.\n");
		exit(1);
	      }
	    if(piclib_link(libobj) != 0)
	    	reason_exit("Could not link library file \"%s\"\n",optarg);

	    piclib_free(libobj);
	    break;
	  }
	case OUTPUT_LIST:
	  lst_file = fopen(optarg,"w");
	  if(lst_file == NULL)
	    lst_file = stdout;
	  break;
	case OUTPUT_LINKAGE:
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
	case 'v':
	  printf("%s\n",PACKAGE_STRING);
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
      if(input == NULL)
        reason_exit("Could not open file \"%s\" for reading.\n",argv[optind-1]);        
      yyin = input;
    }
  else
    printf("Welcome to the piclang compiler.\n");
 
  global_subroutines_GLOBALS = get_subroutine("GLOBALS");// this stores global stuff
  global_subroutines = global_subroutines_GLOBALS;
  g_curr_subroutine = global_subroutines;

  // If string_handler is null, try to malloc it. If it's still null, there's a problem.
  if(string_handler == NULL)
      string_handler = (struct subroutine_map *)malloc(sizeof(struct subroutine_map));
  if(string_handler == NULL)
	reason_exit("Could not allocate memory for string_handler\n");
 
  yyparse();

  if(hex_file == stdout)
    printf("Here comes your code.\nThank you come again.\nCODE:\n");
  
  if(compile_only)
    {
      pasm_compile(eeprom_file,hex_file,global_subroutines,&piclang_bitmap);
      write_piclib_obj(binary_file,global_subroutines);
    }
  else
    {
      finished_code = pasm_build(binary_file,eeprom_file,hex_file,global_subroutines,&piclang_bitmap);
    }

  create_lst_file(lst_file, finished_code);
  
  create_lnk_file(lnk_file, finished_code); 

  all_free_subroutines(global_subroutines);
  free_all_code(finished_code);

  return 0;
}

void fline_message(FILE *file, char *s)
{
  if(file == NULL)
    file = stdout;
  fprintf(file, "(Line");
  if(yylloc.first_line != yylloc.last_line)
    fprintf(file,"s");
  fprintf(file," %d",yylloc.first_line);
  if(yylloc.first_line != yylloc.last_line)
    fprintf(file," - %d",yylloc.last_line);
  
  fprintf(file,") %s ",s);
}

void yyerror(char *s) {
  extern char *yytext;
  fline_message(stdout, s);
  fprintf(stdout," %s\n",yytext);
  exit(-1);
}

