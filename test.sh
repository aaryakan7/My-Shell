# Conditionals are invalid before any command has run.
and echo BAD_INITIAL_CONDITIONAL
or echo CHECK_INITIAL_CONDITIONAL

# Create files and directories used below.
mkdir -p testdir sub
touch file1.txt file2.txt a.c sub/a.c .hiddenfile visiblefile

# Basic commands, comments, and argument parsing.
pwd
echo hello from batch
echo hello # trailing comment
echo before > comment.txt#after
grep -F -x before comment.txt
and echo CHECK_INLINE_COMMENT
/bin/pwd
ls
which ls
which missing_command_for_mysh
which cd
cd
cd testdir extra

# Directory changes and built-in redirection.
cd testdir
pwd
cd ..
cd missing_directory_for_mysh
pwd > pwd.txt
cat pwd.txt
grep -q ^/ pwd.txt
and echo CHECK_PWD_REDIRECT
which ls > which.txt
cat which.txt
grep -q /ls$ which.txt
and echo CHECK_WHICH_REDIRECT

# File redirection.
echo first > output.txt
cat output.txt
echo replacement > output.txt
cat output.txt
cat < output.txt
sort < output.txt > sorted.txt
cat sorted.txt

# Pipelines, including built-ins.
echo pipeline | wc -c
pwd | cat
and echo CHECK_PWD_PIPE
which ls | cat
and echo CHECK_WHICH_PIPE
missing_command_for_mysh | wc -l
and echo CHECK_PIPE_LAST_SUCCESS
echo pipeline | missing_command_for_mysh
or echo CHECK_PIPE_LAST_FAILURE

# Conditional execution.
true
and echo CHECK_AND
false
and echo and-skipped
false
or echo CHECK_OR
true
or echo or-skipped
false
or true
and echo chained-condition-runs

# Syntax errors fail and allow the next conditional command to run.
echo incomplete |
or echo CHECK_PIPE_SYNTAX
echo incomplete >
or echo CHECK_REDIRECT_SYNTAX
echo too many pipes | cat | wc -l
or echo CHECK_EXTRA_PIPE_SYNTAX

# Wildcards: matches, no match, paths, hidden files, and multiple patterns.
echo *.txt > wildcard.txt
grep -F -q file1.txt wildcard.txt
and echo CHECK_WILDCARD
echo *.dat
echo sub/*.c
echo *file
echo *.txt *.c

# Redirection failures set the status used by or.
pwd > missing_directory_for_mysh/output.txt
or echo CHECK_REDIRECT_FAILURE

# The shell exits after completing this pipeline.
echo hello | exit
echo should-not-run
