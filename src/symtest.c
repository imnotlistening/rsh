/**
 * Some tests for the symbol table stuff.
 */

#include <stdio.h>
#include <stdlib.h>

#include <symbol_table.h>

extern struct sym_table table;

int main(){

  int err = symtable_init(2);
  if ( err ){
    printf("LoL, just LoL.\n");
    exit(1);
  }

  //printf("Table data address: %p\n", table.tbl_entries);

  /* Now add some symbols. */
  symtable_add("sym1", "a symbol's data");
  symtable_add("sym2", "blah");
  symtable_add("sym3", "skdjhf");

  /* Replace some symbols. */
  symtable_add("sym3", "HI");
  symtable_add("sym8", NULL);
  symtable_add("symsymsym", "meh");
  symtable_add("sym1", "sd");
  symtable_add("sym1", "1");
  symtable_add("sym1", "2");
  symtable_add("sym1", "3");

  printf("Getting symbol: 'sym3': %s\n", symtable_get("sym3"));
  printf("Getting symbol: 'sym3': %s\n", symtable_get("sym3"));
  printf("Getting symbol: 'sym3': %s\n", symtable_get("sym3"));

  printf("Getting symbol: 'sym1': %s\n", symtable_get("sym1"));
  printf("Getting symbol: 'sym1': %s\n", symtable_get("sym1"));
  printf("Getting symbol: 'sym1': %s\n", symtable_get("sym1"));
  symtable_add("sym1", "7");
  printf("Getting symbol: 'sym1': %s\n", symtable_get("sym1"));

  symtable_add("0", "com_name");
  symtable_add("1", "arg0");
  symtable_add("2", "arg1");
  symtable_add("3", "arg2");
  symtable_add("8", "argx");
  _symtable_display();

  printf("----------------------\n");

  struct sym_entry *status = NULL;
  char *name;
  char *value;
  while ( symtable_numeric(&status, &name, &value) ){
    printf("%s -> %s\n", name, value);
  }

  status = NULL;
  while ( symtable_numeric(&status, &name, &value) ){
    printf("%s -> %s\n", name, value);
  }

  printf("--------------------\n");

  symtable_remove("0");
  symtable_remove("1");
  symtable_remove("2");
  symtable_remove("3");
  symtable_remove("8");
  symtable_remove("sym1");
  symtable_remove("sym2");
  symtable_remove("sym3");
  symtable_remove("sym8");
  symtable_remove("symsymsym");
  symtable_remove("bkfhd");

  symtable_add("BLAH", "hello");
  symtable_add("37", "a_script");
  //symtable_add("38", "a_script");

  printf("Symtable:\n");
  _symtable_display();
  printf("--\n");
  
  status = NULL;
  while ( symtable_numeric(&status, &name, &value) ){
    printf(": %s -> %s\n", name, value);
  }

  status = NULL;
  while ( symtable_numeric(&status, &name, &value) ){
    printf(": %s -> %s\n", name, value);
  }

  return 0;

}
