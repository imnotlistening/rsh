#
# Build my RIT Shell (RSH).
#

CC       = gcc
LD       = gcc          # Makes life easier :).
CFLAGS   = -Wall -ggdb 
CPPFLAGS = -I../include #-D_HAVE_GNU_READLINE
LDFLAGS  =
LIBS     = -L../libs -lreadline -lhistory

OBJECTS  = lexxer.o shell_start.o parser.o shellcore.o symbol_table.o exec.o \
		command.o readterm.o prompt.o builtin.o source.o rsh_fd.o
TESTS    = more_tests symtest exectest termtest

all: rsh $(TESTS)

#
# Configure readline. To use these targets extract a gnu-term-readline dist
# into the source directory.
config-readline:
	if [ ! -e .readline_configed ]; then \
		cd readline-6.1 && ./configure; \
	fi
	touch .readline_configed

#
# Build gnu readline. This makes the shell pretty cool.
gnu-readline: config-readline
	cd readline-6.1 && make
	cp readline-6.1/shlib/libhistory.so.6.1 ../lib
	cp readline-6.1/shlib/libreadline.so.6.1 ../lib

#
# Special target for lexxer.o since its generated via the lexxer `flex'.
lexxer.o: parser.lex
	flex parser.lex
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o lexxer.o lex.yy.c

#
# This is used for the rest of the C code.
.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.c: 
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(OBJECTS) $(LIBS)

rsh: $(OBJECTS) main.c
	$(LD) -o rsh main.c $(OBJECTS) $(LIBS)

exectest: $(OBJECTS) exectest.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o exectest exectest.c $(OBJECTS)

symtest: $(OBJECTS) symtest.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o symtest symtest.c $(OBJECTS)

more_tests: $(OBJECTS) more_tests.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o more_tests more_tests.c $(OBJECTS)

termtest: $(OBJECTS) termtest.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o termtest termtest.c $(OBJECTS)

### Cleaning targets.

clean:
	rm -f $(OBJECTS) lex.yy.c y.tab.c rsh $(TESTS)

clean-all: clean
	rm -f .readline_configed
	if [ -d readline-6.1 ]; then \
		cd readline-6.1 && make distclean ; \
	fi
