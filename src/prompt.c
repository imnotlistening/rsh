/*
 * This, I have a feeling may end up being some what more sophisticated. I
 * really like my bash/zshrc prompt, so I may implement some of those features.
 * but not yet. For now this will remain really stupid.
 */

#include <rsh.h>
#include <symbol_table.h>

#include <stdio.h>

void _display_user_name();
void _display_mach_name();
void _display_dirn_name();


/*
 * Just make sure the symbol table has the default prompt set up.
 */
void prompt_init(){

  symtable_add("PROMPT", "[rsh]$ ");

}

/*
 * Print the contents of $PROMPT. Maybe $PROMPT will be able to contain clever
 * escape sequences some day, but for now, it just prints the prompt character
 * by character. OK, now some escapes are implemented via the '%' character.
 * For instance, %n evaluates to the user name that the shell is running under.
 */
void prompt_print(){

  char *prompt = symtable_get("PROMPT");
  if ( ! prompt ){
    /* Bug... call prompt_init() before you call this. */
    printf("> ");
    fflush(stdout);
    return;
  }

  while ( *prompt ){

    /* We must print our character. */
    if ( *prompt == '%' ){

      prompt++;
      switch ( *prompt ){

      case '%': /* Just print a '%' character. */
	putchar('%');
	break;
      case 'n': /* User name. */
	break;
      case 'm': /* Machine name. */
	break;
      case '1': /* The directory name that you are in. ~ = $HOME */
	break;
      case 'h':
	break;
      case 'H':
	break;
      }

    } else {
      /* Otherwise, just print the character. */
      putchar(*prompt);
    }

    prompt++;

  }
  printf("%s", prompt);
  fflush(stdout);

}

