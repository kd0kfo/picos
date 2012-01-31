%{
#include "pasm.h"
#include "../piclang.h"
#include "utils.h"
#include "../page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <getopt.h>
#include <errno.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

struct compiled_code *the_code;
struct compiled_code *the_code_end;
struct compiled_code *the_strings;
struct compiled_code *the_strings_end;
struct subroutine_map *subroutines;

extern struct assembly_map opcodes[];

char **string_list;
size_t num_strings;
picos_size_t FS_BUFFER_SIZE;

idNodeType *variable_list = NULL;// Variable table
extern picos_size_t label_counter;
 int break_to_label, continue_to_label;

/* prototypes */
nodeType *opr(int oper, int nops, ...);
nodeType *id(idNodeType var);
nodeType *con(int value);
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
%type <nPtr> stmt expr stmt_list STRING SUBROUTINE

%token WHILE BREAK CONTINUE IF CALL SUBROUTINE
%token STRING RETURN DEFINE EXIT 
%token PASM_CR PASM_POP ARGV ARGC FIN FEOF STATEMENT_DELIM

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
        | VARIABLE '=' expr ';'          { $$ = opr(PICLANG_POP, 2, id($1), $3); }
        | WHILE '(' expr ')' stmt        { $$ = opr(PASM_WHILE, 2, $3, $5); }
        | BREAK ';'                      { $$ = opr(PASM_BREAK, 0); }
        | CONTINUE ';'                   { $$ = opr(PASM_CONTINUE, 0); }
        | IF '(' expr ')' stmt %prec IFX { $$ = opr(PASM_IF, 2, $3, $5); }
        | IF '(' expr ')' stmt ELSE stmt { $$ = opr(PASM_IF, 3, $3, $5, $7); }
        | '{' stmt_list '}'              { $$ = $2; }
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

void insert_subroutine(const char *name, size_t label)
{
  
  if(subroutines == NULL)
    subroutines = (struct subroutine_map*)malloc(sizeof(struct subroutine_map));
  else
    {
      struct subroutine_map* tmp = (struct subroutine_map*)malloc(sizeof(struct subroutine_map));
      tmp->next = subroutines;
      subroutines = tmp;
    }
  strcpy(subroutines->name,name);
  subroutines->label = label;
}

#define SIZEOF_NODETYPE ((char *)&p->con - (char *)p)

nodeType *con(int value) {
    nodeType *p;
    size_t nodeSize;

    /* allocate node */
    nodeSize = SIZEOF_NODETYPE + sizeof(conNodeType);
    if ((p = malloc(nodeSize)) == NULL)
        yyerror("out of memory");

    /* copy information */
    p->type = typeCon;
    p->con.value = value;

    return p;
}

int handle_string(const char *pStr)
{
  int retval = -1;
  if(pStr != NULL)
    {
      int is_new = TRUE;
      retval = resolve_string(pStr,&is_new) + PICLANG_STRING_OFFSET;// when referencing strings, arguments will go first.
      if(is_new)
	{
	  if(assembly_file != NULL)
	    write_assembly(assembly_file,"\t; \"%s\" = %d\n", pStr,retval);
	  while(pStr != NULL)
	    {
	      insert_string(*pStr);
	      if(*pStr == 0)
		break;
	      pStr++;
	    }
	}
    }
  else
    {
      yyerror("Invalid string");
    }
  return retval;
}

nodeType *id(idNodeType variable_node) {
    nodeType *p;
    size_t nodeSize;

    /* allocate node */
    nodeSize = SIZEOF_NODETYPE + sizeof(idNodeType);
    if ((p = malloc(nodeSize)) == NULL)
        yyerror("out of memory");

    /* copy information */
    p->type = typeId;
    p->id.i = variable_node.i;
    strcpy(p->id.name, variable_node.name);
    p->id.next = NULL;

    return p;
}

nodeType *opr(int oper, int nops, ...) {
    va_list ap;
    nodeType *p;
    size_t nodeSize;
    int i;

    /* allocate node */
    nodeSize = SIZEOF_NODETYPE + sizeof(oprNodeType) +
        (nops - 1) * sizeof(nodeType*);
    if ((p = malloc(nodeSize)) == NULL)
        yyerror("out of memory");

    /* copy information */
    p->type = typeOpr;
    p->opr.oper = oper;
    p->opr.nops = nops;
    va_start(ap, nops);
    for (i = 0; i < nops; i++)
        p->opr.op[i] = va_arg(ap, nodeType*);
    va_end(ap);
    return p;
}

void freeNode(nodeType *p) {
    int i;

    if (!p) return;
    if (p->type == typeOpr) {
        for (i = 0; i < p->opr.nops; i++)
            freeNode(p->opr.op[i]);
    }
    free (p);
}

void yyerror(char *s) {
  fprintf(stdout, "(%d - %d) %s: %s\n",yylloc.first_line,yylloc.last_line, s,yytext);
    exit(-1);
}


void write_val_for_pic(FILE *binary_file,picos_size_t val)
{
  picos_size_t endiantest = 1;
  int size;
  unsigned char is_littleendian, *end; 
  if(binary_file == NULL)
    return;

  // Ensure little endian is used.
  size = sizeof(val);// in bytes
  end = (char*)&endiantest;
  is_littleendian = *end;
  if(is_littleendian)
    {
      //little endian
      end = (char*)&val;
      while(size > 0)
	{
	  fprintf(binary_file,"%c",*end);
	  end++;
	  size--;
	}
    }
  else
    {
      while(size > 0)
	{
	  fprintf(binary_file,"%c",(val & 0xff));
	  val >> 8;
	  size--;
	}
    }
  
}


static const char short_options[] = "a:b:e:hl:o:";
enum OPTION_INDICES{OUTPUT_HEX};
static struct option long_options[] =
             {
	       {"help",0,NULL,'h'},
               {"hex", 1,NULL, OUTPUT_HEX},
	       {"asm", 1,NULL, 'a'},
	       {"eeprom",1,NULL, 'e'},
	       {"binary",1,NULL,'o'},
	       {"list",1,NULL,'l'},
	       {"buffer_size",1,NULL,'b'},
               {0, 0, 0, 0}
             };

void print_help()
{
  printf("\n");
  printf("pasm -- Piclang compiler.\n");
  printf("Copyright 2011 David Coss, PhD\n");
  printf("-------------------------------\n");
  printf("Compiles piclang programs for use with the Pic Operating System.\n");
  printf("Note: If no source file is provided, the compiler will act as \n\tan interpreter.\n");
  printf("\n");
  printf("Usage: pasm [options] [source code]\n\n");
  printf("Options:\n");
  printf("--help, -h :\t\t Displays this dialog.\n");
  printf("--asm,-a <file> :\t Outputs the assembly to the specified file.\n");
  printf("--hex <file>    :\t Outputs Intel Hex to the specified file.\n");
  printf("--eeprom, -e <file> :\t Outputs \"__EEPROM_DATA(...)\" code for use\n");
  printf("                     \t with the Hi Tech C Compiler.\n");
  printf("--binary, -o <file> :\t Outputs a binary file containing the compiled program.\n");
  printf("--list, -l <file> :\t Outputs a list of program addresses (PC values) for each assembly entry.\n");
  printf("--block_size, -b <INT> :\t Sets the size of block of the target PICFS (Default: 128)");
}

int main(int argc, char **argv) 
{
  char hex_buffer[45];
  FILE *hex_file = NULL, *eeprom_file = NULL, *binary_file = NULL;
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
  
  while(TRUE)
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
  insert_code(PICLANG_PUSHL);
  insert_code(PICLANG_SUCCESS);
  insert_code(PICLANG_EXIT);

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

  if(lst_file != NULL)
    {
      struct assembly_map* curr;
      int code_counter = 0;
      curr_code = the_code;
      for(;curr_code != NULL;curr_code = curr_code->next)
	{
	  if(curr_code->type == typePCB)
	    continue;
	  if(curr_code->type == typeStr)
	    break;
	  curr = opcode2assembly(curr_code->val);
	  fprintf(lst_file,"(%d)\t",code_counter++);
	  switch(curr->opcode)
	    {
	    case PICLANG_NUM_COMMANDS:
	      if(curr_code->type == typeLabel)
		{
		  fprintf(lst_file,"L%03hu",curr_code->label);
		  break;
		}
	      else if(curr_code->val == PICLANG_RETURN)
		{
		  fprintf(lst_file,"return");
		  break;
		}
	      else if(curr_code->val == PICLANG_CALL)
		{
		  curr_code = curr_code->next;
		  fprintf(lst_file,"call %hu",curr_code->val);
		  code_counter++;
		  break;
		}
	    default:
	      fprintf(lst_file,"%s",curr->keyword);
	      break;
	    }
	  if(curr->has_arg)
	    {
	      curr_code = curr_code->next;
	      code_counter++;
	      fprintf(lst_file," %d",curr_code->val);
	      if(curr->has_arg > 1)
		{
		  int arg_counter = 1;
		  for(;arg_counter < curr->has_arg;arg_counter++)
		    {
		      code_counter++;
		      curr_code = curr_code->next;
		      fprintf(lst_file,", %d",curr_code->val);
		    }
		}
	    }
	  fprintf(lst_file,"\n");
	}
      // print strings
      if(curr_code != NULL)
	fprintf(lst_file,"Strings:\n\"");
      for(;curr_code != NULL;curr_code = curr_code->next)
	{
	  if(curr_code->val == 0)
	    {
	      fprintf(lst_file,"\"\n");
	      if(curr_code->next != NULL)
		fprintf(lst_file,"\"");
	    }
	  else if(curr_code->val == '"')
	    fprintf(lst_file,"\"%c",curr_code->val);
	  else
	    fprintf(lst_file,"%c",curr_code->val);
	}
    }
  
  FreeCode(the_code);
  return 0;
}

int ex(nodeType *p) {
  int lbl1, lbl2;
  int previous_break_to_label = break_to_label;
  int previous_continue_to_label = continue_to_label;
  if (!p) return 0;
  switch(p->type) {
  case typeCon:
    write_assembly(assembly_file,"\tpushl\t0x%x\n", p->con.value); 
    insert_code(PICLANG_PUSHL);
    insert_code(p->con.value);
    break;
  case typeId:        
    write_assembly(assembly_file,"\tpush\t%s\n", p->id.name);
    insert_code(PICLANG_PUSH);
    insert_code(resolve_variable(p->id.name));
    break;
  case typeOpr:
    switch(p->opr.oper) {
    case PICLANG_EXIT:
      if(p->opr.nops == 0)
	{
	  write_assembly(assembly_file,"\tpushl\t0\n",0); 
	  insert_code(PICLANG_PUSHL);
	  insert_code(0);
	}
      else
	ex(p->opr.op[0]);
      write_assembly(assembly_file,"\texit\n",0); 
      insert_code(PICLANG_EXIT);
      break;
    case PICLANG_PUSH:
      ex(p->opr.op[0]);
      break;
    case PICLANG_CALL:
      {
	const struct subroutine_map *subroutine = get_subroutine(p->opr.op[0]->str.string);
	write_assembly(assembly_file,"\tcall\tL%03d\n",subroutine->label);
	insert_code(PICLANG_CALL);
	insert_code(subroutine->label);
	break;
      }
    case PASM_DEFINE:// KEEP RETURN AFTER DEFINE
      {
	const char *subroutine = p->opr.op[0]->str.string;
	write_assembly(assembly_file,"L%03d:\t;<%s>\n", (lbl1 = label_counter),subroutine);
	label_counter++;
	insert_label(PICLANG_LABEL,lbl1);
	insert_subroutine(subroutine,lbl1);
	ex(p->opr.op[1]);
	if(strcmp(subroutine,"main") == 0)
	  {
	    write_assembly(assembly_file,"\texit\n");//eop will be written by the compile routine
	    break;
	  }
      }
    case PICLANG_RETURN:// KEEP RETURN AFTER DEFINE
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\treturn\n");
      insert_code(PICLANG_RETURN);
      break;
    case PASM_CONTINUE:
      if(continue_to_label < 0)
	{
	  yyerror("Not within a block in which to continue");
	  exit(-1);
	}
      write_assembly(assembly_file,"\tjmp\tL%03d\n", continue_to_label);
      insert_code(PICLANG_JMP);
      insert_code(continue_to_label);
      break;
    case PASM_BREAK:
      if(break_to_label < 0)
	{
	  yyerror("Not within a block from which to break");
	  exit(-1);
	}
      write_assembly(assembly_file,"\tjmp\tL%03d\n", break_to_label);
      insert_code(PICLANG_JMP);
      insert_code(break_to_label);
      break;
    case PASM_WHILE:
      write_assembly(assembly_file,"L%03d:\n", (lbl1 = label_counter));
      continue_to_label = lbl1;
      insert_label(PICLANG_LABEL,lbl1);
      label_counter++;
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tjz\tL%03d\n", (lbl2 = label_counter));
      label_counter++;
      break_to_label = lbl2;
      insert_code(PICLANG_JZ);
      insert_code(lbl2);
      ex(p->opr.op[1]);
      write_assembly(assembly_file,"\tjmp\tL%03d\n", lbl1);
      insert_code(PICLANG_JMP);
      insert_code(lbl1);
      write_assembly(assembly_file,"L%03d:\n", lbl2);
      insert_label(PICLANG_LABEL,lbl2);
      break_to_label = previous_break_to_label;
      continue_to_label = previous_continue_to_label;
      break;
    case PASM_IF:
      ex(p->opr.op[0]);
      if (p->opr.nops > 2) {
	/* if else */
	write_assembly(assembly_file,"\tjz\tL%03d\n", (lbl1 = label_counter));
	label_counter++;
	insert_code(PICLANG_JZ);
	insert_code(lbl1);
	ex(p->opr.op[1]);
	write_assembly(assembly_file,"\tjmp\tL%03d\n", (lbl2 = label_counter));
	label_counter++;
	insert_code(PICLANG_JMP);
	insert_code(lbl2);
	write_assembly(assembly_file,"L%03d:\n", lbl1);
	insert_label(PICLANG_LABEL,lbl1);
	ex(p->opr.op[2]);
	write_assembly(assembly_file,"L%03d:\n", lbl2);
	insert_label(PICLANG_LABEL,lbl2);
      } else {
	/* if */
	write_assembly(assembly_file,"\tjz\tL%03d\n", (lbl1 = label_counter));
	label_counter++;
	insert_code(PICLANG_JZ);
	insert_code(lbl1);
	ex(p->opr.op[1]);
	write_assembly(assembly_file,"L%03d:\n", lbl1);
	insert_label(PICLANG_LABEL,lbl1);
      }
      break;
    case PICLANG_PRINT:     
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tputd\n");insert_code(PICLANG_PRINTL);
      break;
    case PICLANG_ARGV:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\targv\n");
      insert_code(PICLANG_ARGV);
      break;
    case PICLANG_ARGC:
      write_assembly(assembly_file,"\targc\n");
      insert_code(PICLANG_ARGC);
      break;
    case PICLANG_PRINTL:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tputch\n");insert_code(PICLANG_PRINT);
      break;
    case PICLANG_FPUTCH:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tfputch\n");insert_code(PICLANG_FPUTCH);
      break;
    case PICLANG_FPUTD:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tfputd\n");insert_code(PICLANG_FPUTD);
      break;
    case PICLANG_FOPEN:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tfopen\n");insert_code(PICLANG_FOPEN);
      break;
    case PICLANG_FCLOSE:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tfclose\n");insert_code(PICLANG_FCLOSE);
      break;
    case PICLANG_FREAD:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tfread\n");insert_code(PICLANG_FREAD);
      break;
    case PICLANG_DROP:
      write_assembly(assembly_file,"\tfdrop\n");
      insert_code(PICLANG_DROP);
      break;
    case PICLANG_SWAP:
      write_assembly(assembly_file,"\tfswap\n");
      insert_code(PICLANG_SWAP);
      break;
    case PICLANG_POP:
      if(p->opr.nops == 0)
	break;
      if(p->opr.nops > 1)
	ex(p->opr.op[1]);
      write_assembly(assembly_file,"\tpop\t%s\n", p->opr.op[0]->id.name);
      insert_code( PICLANG_POP);
      insert_code(resolve_variable(p->opr.op[0]->id.name));
      break;
    case PICLANG_UMINUS:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tneg\n");
      insert_code(PICLANG_UMINUS);
      break;
    case PICLANG_SYSTEM:
      {
	int op_counter = p->opr.nops - 1;
	for(;op_counter >= 0 ;op_counter--)
	  ex(p->opr.op[op_counter]);
	write_assembly(assembly_file,"\tsystem\n");insert_code(PICLANG_SYSTEM);
	break;
      }
    case PICLANG_SIGNAL:
      {
	const struct subroutine_map *subroutine = NULL;
	if(p->opr.nops != 2)
	  {
	    fprintf(stderr,"Invalid number of arguments to signal()\nNeeded 2, got %d\n",p->opr.nops);
	    yyerror("Syntax error");
	  }
	subroutine = get_subroutine(p->opr.op[1]->str.string);
	if(subroutine == NULL)
	  {
	    fprintf(stderr,"Invalid subroutine: %s\n",p->opr.op[1]->str.string);
	    yyerror("Syntax error");
	  }
	write_assembly(assembly_file,"\tsignal %d, L%03d\n", p->opr.op[0]->con.value, subroutine->label);
	insert_code(PICLANG_SIGNAL);
	insert_code(p->opr.op[0]->con.value);
	insert_code(subroutine->label);
	break;
      }
    case PICLANG_SLEEP:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tsleep\n");
      insert_code(PICLANG_SLEEP);
      break;
    case PICLANG_SPRINT:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tsprint\n");
      insert_code(PICLANG_SPRINT);
      break;
    case PICLANG_MORSE:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tmorse\n");
      insert_code(PICLANG_MORSE);
      break;
    case PICLANG_TIME:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\ttime\n");
      insert_code(PICLANG_TIME);
      break;
    case PICLANG_FFLUSH:
      write_assembly(assembly_file,"\tfflush\n");
      insert_code(PICLANG_FFLUSH);
      break;
    case PICLANG_FCLEAR:
      write_assembly(assembly_file,"\tfclear\n");
      insert_code(PICLANG_FCLEAR);
      break;
    case PICLANG_CLEAR:
      write_assembly(assembly_file,"\tclear\n");
      insert_code(PICLANG_CLEAR);
      break;
    case PICLANG_SET_TIME:
      ex(p->opr.op[0]);
      ex(p->opr.op[1]);
      write_assembly(assembly_file,"\tsettime\n");
      insert_code(PICLANG_SET_TIME);
      break;
    case PICLANG_SET_DATE:
      ex(p->opr.op[0]);
      ex(p->opr.op[1]);
      ex(p->opr.op[2]);
      write_assembly(assembly_file,"\tsetdate\n");
      insert_code(PICLANG_SET_DATE);
      break;
    case PICLANG_GETD:
      write_assembly(assembly_file,"\tgetd\n");insert_code(PICLANG_GETD);
      break;
    case PICLANG_GETCH:
      write_assembly(assembly_file,"\tgetch\n");insert_code(PICLANG_GETCH);
      break;
    case PICLANG_NOT:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tnot\n");
      insert_code(PICLANG_NOT);
      break;
    case PICLANG_CHDIR:
      ex(p->opr.op[0]);
      write_assembly(assembly_file,"\tchdir\n");
      insert_code(PICLANG_CHDIR);
      break;
    case PICLANG_PWDIR:
      write_assembly(assembly_file,"\tpwdir\n");
      insert_code(PICLANG_PWDIR);
      break;
    case PICLANG_MOUNT:
      ex(p->opr.op[0]);
      ex(p->opr.op[1]);
      write_assembly(assembly_file,"\tmount\n");
      insert_code(PICLANG_MOUNT);
      break;
    default:// all piclang functions
      {
	ex(p->opr.op[0]);
	ex(p->opr.op[1]);
	switch(p->opr.oper) {
	case PICLANG_DEREF:// array access
	  write_assembly(assembly_file,"\tderef\n");
	  insert_code(PICLANG_DEREF);
	  break;
	case PICLANG_BSR:
	  write_assembly(assembly_file,"\tbsr \n");
	  insert_code(PICLANG_BSR);
	  break;
	case PICLANG_BSL:
	  write_assembly(assembly_file,"\tbsl \n");
	  insert_code(PICLANG_BSL);
	  break;
	case PICLANG_AND:
	  write_assembly(assembly_file,"\tand\n");
	  insert_code(PICLANG_AND);
	  break;
	case PICLANG_OR:
	  write_assembly(assembly_file,"\tor\n");
	  insert_code(PICLANG_OR);
	  break;
	case PICLANG_MOD:
	  write_assembly(assembly_file,"\tmod \n"); 
	  insert_code(PICLANG_MOD);
	  break;
	case PICLANG_ADD:   
	  write_assembly(assembly_file,"\tadd \n"); 
	  insert_code(PICLANG_ADD);
	  break;
	case PICLANG_SUB:   
	  write_assembly(assembly_file,"\tsub\n");
	  insert_code(PICLANG_SUB); 
	  break; 
	case PICLANG_MULT:   
	  write_assembly(assembly_file,"\tmul\n");
	  insert_code(PICLANG_MULT); 
	  break;
	case PICLANG_DIV:   
	  write_assembly(assembly_file,"\tdiv\n"); 
	  insert_code(PICLANG_DIV);
	  break;
	case PICLANG_COMPLT:   
	  write_assembly(assembly_file,"\tcompLT\n"); 
	  insert_code(PICLANG_COMPLT);
	  break;
	case PICLANG_COMPGT:   
	  write_assembly(assembly_file,"\tcompGT\n"); 
	  insert_code(PICLANG_COMPGT);
	  break;
	case PICLANG_COMPNE:    
	  write_assembly(assembly_file,"\tcompNE\n"); 
	  insert_code(PICLANG_COMPNE);
	  break;
	case PICLANG_COMPEQ:    
	  write_assembly(assembly_file,"\tcompEQ\n"); 
	  insert_code(PICLANG_COMPEQ);
	  break;
	case PASM_STATEMENT_DELIM:
	  break;
	default:
	  fprintf(stderr,"Unknown op code: %d\n",p->opr.oper);
	  exit(-1);
	  break;
	}//switch
      }//default
    }
  }
  return 0;
}

int resolve_string(const char *str, int *is_new)
{
  size_t i,retval;
  if(string_list == NULL)
    {
      string_list = (char**)malloc(sizeof(char*));
      string_list[0] = strdup(str);
      num_strings = 1;
      if(is_new != NULL)
	*is_new = TRUE;
      return 0;
    }
  
  if(is_new != NULL)
    *is_new = FALSE;
  i = 0;
  retval = 0;
  for(;i<num_strings;i++)
    if(strcmp(string_list[i],str) == 0)
      return retval;
    else
      retval += strlen(string_list[i]) + 1;
  
  if(is_new != NULL)
    *is_new = TRUE;
  string_list = (char**)realloc(string_list,(num_strings+1)*sizeof(char*));
  string_list[num_strings++] = strdup(str);
  return retval;
}

int count_variables()
{
  int retval = 0;
  const idNodeType *it = variable_list;
  while(it != NULL)
    {
      retval++;
      it = it->next;
    }
  return retval;
}

int resolve_variable(const char *name)
{
  int i;
  idNodeType *curr_variable = variable_list;
  if(name == NULL)
    {
      yyerror("Invalid variable name: NULL POINTER\n");
      return -1;
    }

  if(variable_list == NULL)
    {
      variable_list = (idNodeType*)malloc(sizeof(idNodeType));
      variable_list->i = 0;
      strcpy(variable_list->name,name);
      variable_list->next = NULL;
      return 0;
    }
  
  while(curr_variable != NULL)
    {
      if(strcmp(curr_variable->name,name) == 0)
	return curr_variable->i;
      curr_variable = curr_variable->next;
    }

  // At this point, the variable does not exist.
  curr_variable = (idNodeType*)malloc(sizeof(idNodeType));
  strcpy(curr_variable->name,name);
  curr_variable->i = count_variables();
  curr_variable->next = variable_list;
  variable_list = curr_variable;

  return curr_variable->i;
}

const struct subroutine_map* get_subroutine(const char *name)
{
  const struct subroutine_map *retval = NULL;
  if(name == NULL)
    {
      fprintf(stderr,"NULL pointer for the subroutine name.\n");
      exit(-1);
    }
  if(subroutines == NULL)
    {
      fprintf(stderr,"No subroutines yet defined.\n");
      exit(-1);
    }
  
  retval = subroutines;
  while(retval != NULL)
    {
      if(strcmp(retval->name,name) == 0)
	return retval;
      retval = retval->next;
    }
  fprintf(stderr,"No such subroutine: %s\n",name);
  exit(-1);
}

void set_pcb_type(struct compiled_code *the_pcb)
{
  if(the_pcb == NULL)
    return;
  the_pcb->type = typePCB;
  set_pcb_type(the_pcb->next);
}

struct compiled_code* MakePCB(struct compiled_code *the_code, struct compiled_code *the_strings, int total_memory, picos_size_t piclang_bitmap)
{
  int i;
  int page_size = FS_BUFFER_SIZE;
  
  struct compiled_code *magic_number = NULL;
  struct compiled_code *size = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  struct compiled_code *bitmap = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  bitmap->val = piclang_bitmap;
  struct compiled_code *num_pages = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  num_pages->val = (unsigned char)ceil(1.0*total_memory/PAGE_SIZE);
  struct compiled_code *pc = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  struct compiled_code *status = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  status->val = PICLANG_SUCCESS;
  struct compiled_code *start_address = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  start_address->val = FS_BUFFER_SIZE;
  struct compiled_code *string_address = (struct compiled_code*)malloc(sizeof(struct compiled_code));
  struct compiled_code *stack, *end_of_stack;
  struct compiled_code *call_stack, *end_of_call_stack;
  create_stack(&stack,&end_of_stack,PICLANG_STACK_SIZE);
  end_of_stack->next->val = 0;
  end_of_stack = end_of_stack->next;// stack head

  create_stack(&call_stack,&end_of_call_stack,PICLANG_CALL_STACK_SIZE);
  end_of_call_stack->next->val = 0;
  end_of_call_stack = end_of_call_stack->next;// stack head
  
  // Piece the linked list together
  size->next = bitmap;
  bitmap->next = num_pages;
  num_pages->next = pc;
  pc->next = status;
  status->next = start_address;
  start_address->next = string_address;
  string_address->next = stack;
  end_of_stack->next = call_stack;
  end_of_call_stack->next = NULL;// temporary to count PCB's size and set PCB code types

  // Pad block with zeros
  i = CountCode(size);
  for(;i<FS_BUFFER_SIZE/sizeof(picos_size_t)-sizeof(picos_size_t);i++)
    {
      end_of_call_stack->next = (struct compiled_code*)malloc(sizeof(struct compiled_code));
      end_of_call_stack = end_of_call_stack->next;
      end_of_call_stack->next = NULL;
      end_of_call_stack->val = 0xdead;
    }
  set_pcb_type(size);
  start_address->val = CountCode(size);

  // Pad pages to fit into blocks
  if(page_size % sizeof(picos_size_t) != 0)
    {
      struct compiled_code *code_index = the_code;
      struct compiled_code *next_page;
      page_size -= (page_size % sizeof(picos_size_t));
      
      while(code_index != NULL)
	{
	  i = 0;
	  for(;i<page_size;i += sizeof(picos_size_t))
	    if(code_index == NULL)
	      break;
	    else
	      code_index = code_index->next;
	  
	  if(code_index == NULL)
	    break;

	  next_page = code_index->next;
	  for(;i<FS_BUFFER_SIZE;i+=sizeof(picos_size_t))
	    {
	      code_index->next = (struct compiled_code*)malloc(sizeof(struct compiled_code));
	      code_index = code_index->next;
	      code_index->val = 0;
	    }
	  code_index->next = next_page;
	  code_index = next_page;
	}
    }

  end_of_call_stack->next = the_code;
  string_address->val = CountCode(size);

  if(the_code == NULL)
    {
      fprintf(stderr,"No code to compile!\n");
      exit -1;
    }
  
  // Find the location of the main function
  pc->val = lookup_label(the_code,((struct subroutine_map*) get_subroutine("main"))->label);

  while(the_code->next != NULL)
    the_code = the_code->next;
  if(the_strings != NULL)
    the_code->next = the_strings;
  
  size->val =  CountCode(size);
  
  // Magic number to identify the executable
  i = PCB_MAGIC_NUMBER_OFFSET - 1;
  for(;i >= 0;i--)
    {
      magic_number = (struct compiled_code*)malloc(sizeof(struct compiled_code));
      magic_number->next = size;
      magic_number->val = PICLANG_magic_numbers[i];
      magic_number->type = typePCB;
      size = magic_number;
    }
  

  return size;
}

void pasm_compile(FILE *eeprom_file,FILE *hex_file,struct compiled_code **the_code, struct compiled_code *the_strings, picos_size_t *piclang_bitmap, int num_variables)
{
  char hex_buffer[45];
  void resolve_labels(struct compiled_code* code);

  resolve_labels(*the_code);
  *the_code = MakePCB(*the_code,the_strings,num_variables,*piclang_bitmap);
  memset(hex_buffer,0,(9 + COMPILE_MAX_WIDTH + 2)*sizeof(char));// header + data + checksum
  if(hex_file != NULL)
    {
      fprintf(hex_file,":020000040000FA\n");
      FPrintCode(hex_file,*the_code,0,hex_buffer,0x4200,0,PRINT_HEX);
      fprintf(hex_file,":00000001FF\n");
    }
  if(eeprom_file != NULL)
    FPrintCode(eeprom_file,*the_code,0,hex_buffer,0x4200,0,PRINT_EEPROM_DATA);

}





