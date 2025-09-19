**Authors:**
Aarya Kansara, Subham Jena

**Overview:**

This project implements a simple command‐line shell, mysh, written in C. The shell supports both interactive mode (with prompts and welcome/goodbye messages) and batch mode (reading commands from a file or via stdin). It handles standard features including tokenization, command parsing, input/output redirection, pipelines (supporting up to two processes), conditional execution using the keywords and and or, and wildcard expansion. Built-in commands include cd, pwd, which, exit, and die. For external commands provided by bare names, the shell searches designated directories (namely /usr/local/bin, /usr/bin, and /bin) to find an executable.

**Test Plan & Directory Setup:**

Our testing verifies that the shell correctly distinguishes interactive from batch modes, processes commands and comments, handles both built-in and external commands (along with proper path resolution using our find_executable function), and correctly supports redirection, pipelines, conditionals, and wildcard expansion. For example, running echo hello | exit should print “hello” and then display a goodbye message before terminating. We confirmed that commands like cd change directories and that tokens including wildcards (e.g., *.txt) expand as expected.

To test the shell, we set up a directory structure similar to the following within the PA3 folder:
<pre>
/common/home/ak2397/Documents/P3/
├── mysh.c
├── Makefile
├── test.sh           # batch file for testing
├── input.txt         # Contains test input lines
├── output.txt        # Output file created by redirection tests
├── sorted.txt        # Output file for sorted results
├── sorted_output.txt # Alternative output file for pipeline redirection
├── file1.txt         # For wildcard test: simple file
├── file2.txt         # For wildcard test: simple file
├── a.txt             # For multiple wildcard tests
├── b.txt             # For multiple wildcard tests
├── x.log             # For multiple wildcard tests
├── y.log             # For multiple wildcard tests
├── .hiddenfile       # Hidden file for wildcard tests
├── visiblefile       # Visible file for wildcard tests
├── testdir/          # Subdirectory for testing 'cd' command
├── nodir/            # Directory for redirection error test; set as read-only
│   └── (empty)
└── sub/              # Subdirectory for wildcard path tests
    ├── a.c           # Source file for wildcard expansion tests
    └── b.c           # Source file for wildcard expansion tests
</pre>
**Execution:**

To build the shell, run the command "make" in your terminal

For interactive mode, run "./mysh"
Setup file directory as described above, or edit the test.sh file.

You will see a welcome message and a prompt (mysh>). Enter commands (e.g. pwd, cd subdir, etc.) and type exit to quit (printing a goodbye message).
For batch mode, you can run a test script. For example, if you created test_all.sh containing multiple commands (ensure it ends with an exit), run:

./mysh < test.sh

The shell will execute all the commands in the script (without extra prompts) and then terminate.
