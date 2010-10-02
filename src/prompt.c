/*
 * This, I have a feeling may end up being some what more sophisticated. I
 * really like my bash/zshrc prompt, so I may implement some of those features.
 * but not yet. For now this will remain really stupid.
 */

#include <stdio.h>

#include <rsh.h>
#include <symbol_table.h>

/*
 * Just make sure the symbol table has the default prompt set up.
 */
void prompt_init(){

  symtable_add("PROMPT", "[rsh]$ ");

}

/*
 * Print the contents of $PROMPT. Maybe $PROMPT will be able to contain clever
 * escape sequences some day, but for now, it just prints the prompt character
 * by character.
 */
void prompt_print(){

  char *prompt = symtable_get("PROMPT");
  if ( ! prompt ){
    /* Bug... call prompt_init() before you call this. */
    printf("> ");
    fflush(stdout);
    return;
  }

  printf("%s", prompt);
  fflush(stdout);

}
