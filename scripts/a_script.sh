#
# An RSH script. This should be parseable by the RSH shell. This was a lot of
# work lol. Parsers are complex; thankfully the grammar for a shell is
# *insanely* simple.
#

# Redirection
cat blah > tmp.txt

# Piping
echo hi | grep hi

# More piping
find | grep '.c$' | xargs  grep 'struct rsh_token'

# Piping and redirection
my_command<    tmp.txt   | grep  some_output
  
# Like the above command only the redirection isn't where most people put it.
<tmp.txt     my_command |   grep other_output