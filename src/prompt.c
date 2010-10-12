/*
 * This, I have a feeling may end up being some what more sophisticated. I
 * really like my bash/zshrc prompt, so I may implement some of those features.
 * but not yet. For now this will remain really stupid.
 */

#include <rsh.h>
#include <symbol_table.h>

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void _display_user_name();
void _display_cwd();
void _display_hostname();
void _display_short_hostname();
void _display_shell_name();

extern char *shell_name;

/* User ID and passwd struct for dealing with \u */
uid_t euid;
struct passwd *user_info;

/*
 * Make sure the symbol table has the default prompt set up. Also set up some
 * stuff for the display escape sequences.
 */
void prompt_init(){

  symtable_add("PROMPT", "[\\u@\\h \\W]$ ");
  euid = geteuid();

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

    //printf("<c=%c>", *prompt);

    /* We must print our character. */
    if ( *prompt == '\\' ){

      prompt++;
      switch ( *prompt ){

      case '\\': /* Just print a '%' character. */
	putchar('\\');
	break;
      case 'u': /* User name. */
	_display_user_name();
	break;
      case 'W': /* The directory name that you are in. ~ = $HOME */
	_display_cwd();
	break;
      case 's': /* Shell name */
	_display_shell_name();
	break;
      case 'H': /* Host name */
	_display_hostname();
	break;
      case 'h': /* Host name up to the first dot. */
	_display_short_hostname();
	break;
      case 'e': /* Escape character. */
	putchar(0x1b);
	break;
      case '$': /* If the EUID is 0, print a '#', otherwise print a '$'. */
	if ( euid == 0 )
	  putchar('#');
	else
	  putchar('$');
	break;
      default:
	/* If we don't understand it, ignore it. */
	printf("^");
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

/*
 * Display the user name of the shell process.
 */
void _display_user_name(){

  user_info = getpwuid(euid);
  printf(user_info->pw_name);

}

void _display_cwd(){

  int i;
  char buf[1024]; /* More than enough space, I hope. */
  char *dir;

  memset(buf, 0, 1024);
  if ( ! getcwd(buf, 1023) )
    return;

  if ( strcmp("/", buf) == 0 ){
    printf(buf);
    return;
  }

  /* We don't care about the beginning of the CWD. We (I) only care about the
   * last directory in the CWD. As in /home/alex becomes alex. */
  i = strlen(buf);
  while ( buf[i] != '/')
    i--;
  
  dir = buf + i + 1;
  printf(dir);
    
}

void _display_hostname(){

  char buf[1024]; /* More than enough space, I hope. */

  memset(buf, 0, 1024);
  if ( gethostname(buf, 1024) < 0 )
    return;

  printf(buf);

}

void _display_short_hostname(){

  int i;
  char buf[1024]; /* More than enough space, I hope. */

  memset(buf, 0, 1024);
  if ( gethostname(buf, 1024) < 0 )
    return;

  /* Print up to the first dot. */
  i = 0;
  while ( buf[i] != '.' )
    i++;

  buf[i] = 0;
  printf(buf);

}

void _display_shell_name(){

  char *shell_path = shell_name;
  char *shell_prog = shell_name;

  while ( *shell_path != 0 ){
    if ( *shell_path == '/' )
      shell_prog = shell_path + 1;
    shell_path++;
  }

  printf(shell_prog);

}
