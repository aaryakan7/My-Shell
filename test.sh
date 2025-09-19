# Print current directory.
pwd

# A comment line should be ignored.
# This is a comment and should not be processed.

# Print a custom message.
echo hello from batch

# pwd
pwd

# cd (Valid Directory)
# (Assume a subdirectory named "testdir" exists; if not, create it in your project directory.)
cd testdir
pwd
cd ..

# cd (Non-existent Directory)
cd non_existent_dir

# cd (Wrong Number of Arguments)
cd
cd dir1 dir2

# which (External Command Found)
which ls

# which (External Command Not Found)
which non_existent_command_xyz

# which (Built-in Command)
which cd

# which (Wrong Number of Arguments)
which
which ls pwd

# Simple External Command (Bare Name)
ls

# External Command with Arguments
echo hello world

# External Command (Full Path)
# (Adjust the path if needed based on your system.)
/bin/pwd

# Command Not Found
non_existent_command_xyz

# Output Redirection
echo hello > output.txt

# Verify output redirection:
cat output.txt

# Output Redirection Overwrite
echo new content > output.txt
cat output.txt

# Input Redirection
# For this test, ensure output.txt exists.
cat < output.txt

# Combined Input/Output Redirection
echo "sort me" > input.txt
echo "please" >> input.txt
sort < input.txt > sorted.txt
cat sorted.txt

# Redirection Syntax Error
# These lines are commented out because they are expected to produce errors.
# Uncomment these lines if you want to see the error messages:
# echo error test >
# cat <

# Simple Pipeline
ls | wc -l

# Pipeline with Built-in (First)
pwd | cat

# Pipeline Error (First Command Fails)
non_existent_command | wc -l

# Pipeline Error (Second Command Fails)
ls | non_existent_command

# Pipelines with Redirection in Subcommands
# (These tests are based on the provided code behavior)
cat < input.txt | wc -l
ls | sort > sorted_output.txt
cat sorted_output.txt

# and (Previous Command Succeeded)
pwd
and echo "Success from 'and'!"

# and (Previous Command Failed)
cd non_existent_dir
and echo "This should NOT print."

# or (Previous Command Succeeded)
pwd
or echo "This should NOT print."

# or (Previous Command Failed)
cd non_existent_dir
or echo "Fallback executed from 'or'!"

# Chained Conditionals (and then or)
pwd
and cd non_existent_dir
or echo "Fallback from failed 'and'"

# Chained Conditionals (or then and)
cd non_existent_dir
or pwd
and echo "Success after 'or'"

# Empty Line
# (Simply press Enter in interactive mode; here, an empty line in the script is ignored.)

# Line with Only Comment
# This line is a comment and should be ignored.

# Command with Trailing Comment
pwd # Get current directory

# Comment interrupting tokens
echo hello # world

# Simple Wildcard Match
# For this test, ensure you have files named file1.txt and file2.txt.
echo "Files matching *.txt:"
echo *.txt

# Wildcard No Match
echo "Files matching *.dat (if none exist, pattern is printed):"
echo *.dat

# Wildcard with Path
# For this test, if you have a subdirectory (e.g., sub) with files such as a.c and b.c:
echo "Files matching sub/*.c:"
echo sub/*.c

# Hidden File Wildcard (No Match)
# For this test, if you have a hidden file named .hiddenfile and a visible file named visiblefile:
echo "Files matching *file (should exclude hidden files):"
echo *file

# Multiple Wildcards
# For this test, if you have files a.txt, b.txt, x.log, and y.log:
echo "Files matching *.txt *.log:"
echo *.txt *.log

exit
