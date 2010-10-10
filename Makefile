#
# Builds `rsh'. The primary source code is located in ./src, likewise the
# header code is in ./include. There are also some scripts to play with in the
# ./scripts directory. There is a README file describing some of the 
# architecutal descisions I made. 
#

# Main target.
all:
	cd src && make
	cp src/rsh .
	if [ ! -e os1shell ]; then \
		ln -s rsh os1shell; \
	fi

# This target builds RSH with GNU readline instead of my readline. This is for
# a much more robust and fully featured readline library. On the other hand I
# doubt I would get much credit for implementing history with gnu-term-readline
# :). 
all_gnu_rl:
	cd src && make gnu-readline

clean:
	cd src && make clean
	rm -f rsh os1shell

distclean:
	cd src && make clean-all
	rm -f rsh os1shell