
#ifndef _SYMBOL_TABLE_H
# define _SYMBOL_TABLE_H

/*
 * Basic symbol table entry type.
 */
struct sym_entry {

  char *name;
  char *data;

  struct sym_entry *next;

};

struct sym_table {

  struct sym_entry *tbl_entries;
  int entries;

  /* Looking up symbols is *super* slow at the moment, as such since most
   * times we will really feel that slow down is going to be very localized
   * store the last two accesses. */
  struct sym_entry *cach_add;
  struct sym_entry *cach_get;

  /* Speed up adding entries by keeping track of the last entry. */
  struct sym_entry *last;

};

/* These are the functions to control and access the symbol table. */
int   symtable_init(int start_size);
int   symtable_add(char *sym, char *data);
char *symtable_get(char *sym);
int   symtable_remove(char *sym);
int   symtable_numeric(struct sym_entry **status, char **name, char **data);

/* Some internal/book keeping functions. */
struct sym_entry *_symtable_get_entry(char *sym);
void              _symtable_display();
void              _symtable_dump_entries();

#endif
