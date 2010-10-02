/**
 * RSH symbol table implementation. This maintains two distinct tables: the
 * first is the libc 'env' table; the second is a symbol table local to the
 * shell itself.
 *
 * Bleh looks like I will have to implement some sort of hash table otherwise
 * this would be ridiculously slow. Actually for now I will just make some
 * dumb strcmp based table.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <rsh.h>
#include <symbol_table.h>

/*
 * The symbol table for the shell. Pretty useful, IMHO.
 */
struct sym_table table;

int symtable_init(int start_size){

  /* Clear the table. */
  memset(&table, 0, sizeof(struct sym_table));

  /* Umm... I think we are done here. */
  return RSH_OK;

}

/*
 * This is the real meat of the symbol table. Add a given value to the table.
 * If there is already a symbol with the name in sym then overwrite and free 
 * the old value.
 */
int symtable_add(char *sym, char *data){
 
  struct sym_entry *syment;

  /* It is rude to pass a NULL sym. */
  if ( ! sym )
    return RSH_ERR;

  /* First see if the passed string is in the environment. */
  if ( getenv(sym) )
    return setenv(sym, data, 1);

  /* Find the entry in the symbol table that corresponds to the passed sym. */
  syment = _symtable_get_entry(sym);
  
  if ( ! syment )
    return RSH_ERR;

  /* Overwrite the data. */
  if ( syment->data )
    free(syment->data);

  if ( data ){
    syment->data = strdup(data);
  } else
    syment->data = NULL;

  /* Update the cach_add pointer to point to this entry. */
  table.cach_add = syment;

  return RSH_OK;

}

char *symtable_get(char *sym){

  char *env_sym;
  struct sym_entry *syment;

  /* It is rude to pass a NULL sym. */
  if ( ! sym )
    return NULL;

  env_sym = getenv(sym);
  if ( env_sym )
    return env_sym;

  /* Find the entry in the symbol table that corresponds to the passed sym. 
   * If it didn't exist we will silently just make a new entry in the symbol
   * table. */
  syment = _symtable_get_entry(sym);
  
  /* Don't forget to update the cach_get pointer. */
  table.cach_get = syment;

  return syment->data;

}

/**
 * Make a pointer to a struct sym_entry and set it to NULL. Then Just call this
 * with a pointer to that pointer until this function returns NULL.
 * Ex:
 *
 *   struct sym_entry *ent = NULL;
 *   char *name;
 *   char *val;
 *   while ( (ent = symtable_get_next_numeric(&ent, &name, &val)) != NULL )
 *     // Do stuff.
 *
 * This function returns the value of the symbol, not the name. You will have
 * to pass a non null address of a pointer to hold the name of the symbol.
 */
int symtable_numeric(struct sym_entry **status, char **name, char **data){

  char *end;
  static int done = 0;

  if ( *status == NULL && ! done ){
    *status = table.tbl_entries;
  }

  if ( done ){
    done = 0;
    return 0;
  }

  while ( *status != NULL ){
    
    strtol( (*status)->name, &end, 10);
    if ( *end ){
      /* Not a numeric symbol. Try the next one. */
      *status = (*status)->next;
      continue;
    }

    /* Otherwise, update status, and return ok. */
    *data = (*status)->data;
    if ( name )
      *name = (*status)->name;

    if ( ! (*status)->next )
      done = 1;
    *status = (*status)->next;

    return 1;

  }

  /* If we are here, we have found all of the numeric symbols. Just return a
   * 0 to signify this. */
  return 0;

}

/*
 * Iterate over the table linked list and if we find the symbol, return that
 * symbol table entry. If we hit the end of the list w/o finding the entry,
 * silently make a new entry with the specified name and return said entry.
 */
struct sym_entry *_symtable_get_entry(char *sym){

  struct sym_entry *entry;

  /* First check our cache variables. */
  if ( table.cach_add )
    if ( strcmp(sym, table.cach_add->name) == 0 )
      return table.cach_add;

  if ( table.cach_get )
    if ( strcmp(sym, table.cach_get->name) == 0 )
      return table.cach_get;
  
  /* Ugh fine, I'll do a look up. */
  entry = table.tbl_entries;
  while ( entry != NULL ){
    if ( strcmp(entry->name, sym) == 0)
      break;
    entry = entry->next;
  }

  /* If the symbol is not NULL, then we had a successful compare, so return
   * that entry. */
  if ( entry )
    return entry;

  /* Otherwise we have to allocate an entry and deal with the table book 
   * keeping. */
  entry = (struct sym_entry *)malloc(sizeof(struct sym_entry));
  if ( ! entry )
    return NULL;

  /* Handle the case that this is the first elem being added. */
  if ( ! table.tbl_entries )
    table.tbl_entries = entry;

  if ( table.last )
    table.last->next = entry;
  table.last = entry;

  entry->name = strdup(sym);
  entry->data = NULL;

  table.entries++;

  return entry;

}

void _symtable_display(){

  struct sym_entry *ent = table.tbl_entries;

  while (ent != NULL){
    printf("%s=%s\n", ent->name, ent->data);
    ent = ent->next;
  }

}
