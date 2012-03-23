#include "pasm.h"
#include <stdlib.h>

const struct subroutine_map* lookup_subroutine(int index)
{
  extern struct subroutine_map *subroutines;
  const struct subroutine_map *retval = subroutines;
  while(retval != NULL)
    {
      if(retval->index == index)
	break;
      retval = retval->next;
    }
  
  return retval;
}

int lookup_label(const struct compiled_code* code, picos_size_t label)
{
  int code_counter = 0;
  if(code == NULL)
    return -1;
  for(;code != NULL;code = code->next)
    {
      if(code->type == typePCB)
	continue;
      if(code->type == typeLabel && (code->val == PICLANG_LABEL))
	{
	  if(code->label == label)
	    return code_counter;
	}
      code_counter++;
    }
  return -1;
}

int get_subroutine_addr(struct compiled_code *code_head, struct compiled_code *code)
{
  const struct subroutine_map *subroutine = lookup_subroutine(code->label);
  if(subroutine == NULL || subroutine->label == -1)
    {
      if(subroutine == NULL)
	fprintf(stderr,"Undefined reference: #%d\n",code->label);
      else
	fprintf(stderr,"Undefined reference: %s\n",subroutine->name);
      exit(-1);
    }
  return lookup_label(code_head, subroutine->label);
  
}

void resolve_labels(struct compiled_code* code)
{
  const struct compiled_code* code_head = code;
  int label_addr, subroutine_label;
  if(code == NULL)
    return;
  
  for(;code != NULL;code = code->next)
    {
      switch(code->val)
	{
	case PICLANG_JMP:case PICLANG_JZ:case PICLANG_CALL:// change labels to addresses
	  if(code->next != NULL)
	    {
	      // Next next word should contain the target address.
	      // 1.) If the type of the next word is a subroutine, the label
	      //     of the subroutine must be found. That label is used
	      //     to find the target *address*.
	      // 2.) Otherwise the next byte is the label itself, which is
	      //     used to find the target *address*.
	      if(code->next->val == PASM_SUBROUTINE)
		label_addr = get_subroutine_addr(code_head, code->next);
	      else
		label_addr = lookup_label(code_head, code->next->label);
	      if(label_addr < 0)
		{
		  fprintf(stderr,"Could not resolve label %d\n",code->next->label);
		  return;
		}
	      code->next->val = (picos_size_t)label_addr;
	      code = code->next;
	      continue;
	    }
	  break;
	case PICLANG_SIGNAL:
	  if(code->next != NULL && code->next->next != NULL)
	    {
	      int label_addr;
	      if(code->next->next->val == PASM_SUBROUTINE)
		label_addr = get_subroutine_addr(code_head, code->next->next);
	      else
		label_addr = lookup_label(code_head, code->next->next->label);
	      code->next->next->val = (picos_size_t)label_addr;
	      code = code->next->next;
	      continue;
	    }
	  break;
	default:
	  break;
	}
      if((opcode2assembly(code->val))->has_arg)
	code = code->next;
    }
}


void create_lst_file(FILE *lst_file, const struct compiled_code *the_code, const struct compiled_code *the_strings)
{
  const struct compiled_code *curr_code = NULL;
  if(lst_file != NULL)
    {
      struct assembly_map* curr;
      struct compiled_code *first_string = NULL;
      int code_counter = 0;
      curr_code = the_code;
      for(;curr_code != NULL;curr_code = curr_code->next)
	{
	  if(curr_code->type == typePCB)
	    continue;
	  if(curr_code->type == typeStr)
	    {
	      continue;
	    }
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
		  while(curr_code->next != NULL && curr_code->type == typeStr)
		    curr_code = curr_code->next;
		  fprintf(lst_file,"call %hu",curr_code->val);
		  code_counter++;
		  break;
		}
	    default:
	      fprintf(lst_file,"%s (0x%x) ",curr->keyword,curr_code->val);
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
      first_string = the_strings;
      if(first_string != NULL)
	fprintf(lst_file,"Strings:\n\"");
      for(;first_string != NULL;first_string = first_string->next)
	{
	  if(first_string->val == 0)
	    {
	      fprintf(lst_file,"\"\n");
	      if(first_string->next != NULL)
		fprintf(lst_file,"\"");
	    }
	  else if(first_string->val == '"')
	    fprintf(lst_file,"\"%c",first_string->val);
	  else
	    fprintf(lst_file,"%c",first_string->val);
	}
    }
}

static struct compiled_code* increment_word(const struct compiled_code *word, int *counter)
{
  if(word == NULL)
    return NULL;
  
  if(counter != NULL)
    *counter += 1;
  return word->next;
}

void create_lnk_file(FILE *lnk_file, const struct compiled_code *the_code)
{
  const struct compiled_code *curr_code = the_code;
  int word_counter = 0,arg_counter;
  struct assembly_map *asmb = NULL;
  if(lnk_file == NULL || curr_code == NULL)
    return;
  
  for(;curr_code != NULL;curr_code = increment_word(curr_code,NULL))
    {
      if(curr_code->type != typeCode && curr_code->type != typeLabel && curr_code->type != typeSubroutine)
	continue;
      word_counter++;
      asmb = opcode2assembly(curr_code->val);
      if(asmb == NULL)
	continue;
      arg_counter = 0;
      switch(curr_code->val)
	{
	case PICLANG_LABEL:
	  fprintf(lnk_file,"Label %d @ %d\n",curr_code->label,word_counter-1);
	  break;
	case PICLANG_JMP: case PICLANG_JZ: case PICLANG_CALL:
	  if(curr_code->next != NULL)
	    fprintf(lnk_file,"%s to %d @ %d\n",asmb->keyword,curr_code->next->val,word_counter);
	  break;
	default:
	  break;
	}
      for(;arg_counter<asmb->has_arg;arg_counter++)
	curr_code = increment_word(curr_code,&word_counter);
    }
}


