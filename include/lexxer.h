
#ifndef _LEXXER_H
# define _LEXXER_H

/* Symbol definitions for the lexxer. */
#define NULL_LEX       0
#define WORD           1   /* Post preprocces */
#define SYMDEFINITION  2   /* Post preprocces */
#define SYMBOL         3
#define TERMINATOR     4
#define BACKTICK       5
#define SINGLEQUOTE    6
#define DOUBLEQUOTE    7
#define OBRACE         8
#define EBRACE         9
#define GLOB           10  
#define REDIRECT_IN    11  /* Post preprocces */
#define REDIRECT_OUT   12  /* Post preprocces */
#define REDIRECT_ERR   13  /* Post preprocces */
#define APPEND_OUT     14  /* Post preprocces */
#define APPEND_ERR     15  /* Post preprocces */
#define PIPE           16  /* Post preprocces */
#define WHITESPACE     17  /* Post preprocces */
#define COMMENT        18
#define EOFTOKEN       19
#define BACKGROUND     20  /* Post preprocces */
#define ESCAPESEQ      21  /* Post preprocces */
#define SYMARGS        22  

#include <stdio.h>

/* Variables that we can access to talk to the lexxer. */
extern FILE *yyin;

/* These are the input functions for RSH. */
size_t rsh_readbuf(char *buffer, size_t max);
void   rsh_set_input(int fd);


#endif
