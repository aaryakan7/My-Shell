## CS 214 Spring 2025 - Project III: My Shell

Authors: Aarya Kansara and Subham Jena

mysh is a C shell that runs one command per line. It can read commands from a file or standard input, and it has an interactive prompt when run in a terminal.

Commands and features
- Built-ins: cd, pwd, which, exit, and die
- External commands found by path or searched in /usr/local/bin, /usr/bin, and /bin
- Input and output redirection with < and >
- Pipelines with two commands
- Conditional commands using and and or
- Wildcard expansion for one * in the final path segment
- Comments starting at # and continuing to the end of the line

Separate <, >, and | from other tokens with whitespace. This shell does not support quoted strings or >>. A pipeline succeeds when its last command succeeds.

Build and run
Run make to build mysh.
Run ./mysh for interactive mode.
Run ./mysh commands.txt to read commands from a file.
Run ./mysh < commands.txt to read commands from standard input.

Testing
Run make test to build mysh and run the checks. The test script creates its own files and directories, and make test runs it in a temporary directory. It checks redirection, pipelines, conditionals, syntax errors, comments, wildcards, and exit behavior.

Run ./mysh test.sh to run the test script manually. It creates its fixtures in the current directory.

Built-ins in a pipeline run in a child process, so cd in a pipeline does not change the shell's working directory. exit ignores its input; for example, echo hello | exit ends the shell after the pipeline but does not print hello to the terminal.
