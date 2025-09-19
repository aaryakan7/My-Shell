#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#define MAX_LINE 1024
#define MAX_TOKENS 100
#define MAX_ARGS 100

// List of directories in which to search for executables when a bare name is used.
const char *search_dirs[] = { "/usr/local/bin", "/usr/bin", "/bin" };
const int num_search_dirs = 3;

// Struct for holding a command and its execution parameters.
typedef struct command {
    char *args[MAX_ARGS];  // Argument list for execv (NULL terminated)
    char *input_file;      // For input redirection (if any)
    char *output_file;     // For output redirection (if any)
} command;

// Function Prototypes
char *find_executable(const char *cmd);
int execute_command(command *cmd);
int execute_pipeline(command *cmd1, command *cmd2);
void run_shell(FILE *input_stream, int interactive);
void trim_newline(char *str);
void tokenize_line(char *line, char *tokens[], int *num_tokens);
int expand_token(const char *token, char *expanded_tokens[], int max_expanded);

/*
 * If the token contains an asterisk (*), expand it to match files in the directory.
 * Assume the asterisk occurs only in the final path segment.
 * If no files match, the original token is returned.
 */
int expand_token(const char *token, char *expanded_tokens[], int max_expanded) {
    if (strchr(token, '*') == NULL) {
        expanded_tokens[0] = strdup(token);
        return 1;
    }
    // Determine the directory and pattern.
    const char *last_slash = strrchr(token, '/');
    char directory[1024];
    const char *pattern;
    if (last_slash) {
        size_t dir_len = last_slash - token;
        if (dir_len >= sizeof(directory)) {
            dir_len = sizeof(directory) - 1;
        }
        strncpy(directory, token, dir_len);
        directory[dir_len] = '\0';
        pattern = last_slash + 1;
    } else {
        strcpy(directory, ".");
        pattern = token;
    }
    
    // Split pattern into prefix and suffix at the asterisk.
    const char *asterisk = strchr(pattern, '*');
    int prefix_len = asterisk - pattern;
    char prefix[256];
    if (prefix_len >= (int)sizeof(prefix)) {
        prefix_len = sizeof(prefix) - 1;
    }
    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';
    const char *suffix = asterisk + 1;
    
    DIR *dirp = opendir(directory);
    if (!dirp) {
        expanded_tokens[0] = strdup(token);
        return 1;
    }
    
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dirp)) != NULL && count < max_expanded) {
        // Skip hidden files unless prefix starts with '.'
        if (entry->d_name[0] == '.' && prefix[0] != '.') {
            continue;
        }
        // Check if the file name starts with the prefix.
        if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
            int name_len = strlen(entry->d_name);
            int suffix_len = strlen(suffix);
            if (name_len >= prefix_len + suffix_len) {
                if (strcmp(entry->d_name + name_len - suffix_len, suffix) == 0) {
                    char fullpath[1024];
                    int n;
                    if (strcmp(directory, ".") == 0) {
                        n = snprintf(fullpath, sizeof(fullpath), "%s", entry->d_name);
                    } else {
                        n = snprintf(fullpath, sizeof(fullpath), "%s/%s", directory, entry->d_name);
                    }
                    if (n < 0 || n >= (int)sizeof(fullpath)) {
                        continue;  // Skip if truncated.
                    }
                    expanded_tokens[count++] = strdup(fullpath);
                }
            }
        }
    }
    closedir(dirp);
    if (count == 0) {  // no matches: return the original token.
        expanded_tokens[0] = strdup(token);
        return 1;
    }
    return count;
}

// Determine interactive versus batch mode and start the shell loop.
int main(int argc, char *argv[]) {
    int interactive = 0;
    FILE *input_stream = NULL;
    
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    if (argc == 2) {
        input_stream = fopen(argv[1], "r");
        if (!input_stream) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }
        interactive = 0;
    } else {
        input_stream = stdin;
        interactive = isatty(STDIN_FILENO);
    }
    
    run_shell(input_stream, interactive);
    
    if (input_stream != stdin) {
        fclose(input_stream);
    }
    
    return 0;
}

// Main loop that reads, tokenizes, parses, and executes commands.
void run_shell(FILE *input_stream, int interactive) {
    char line[MAX_LINE];
    int prev_exit_status = 0;
    
    if (interactive) {
        printf("Welcome to my shell!\n");
    }
    
    while (1) {
        if (interactive) {
            printf("mysh> ");
            fflush(stdout);
        }
        
        if (fgets(line, sizeof(line), input_stream) == NULL) {
            break;
        }
        
        trim_newline(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        char *tokens[MAX_TOKENS];
        int num_tokens = 0;
        tokenize_line(line, tokens, &num_tokens);
        if (num_tokens == 0) {
            continue;
        }
        
        // Check syntax: redirection tokens must have a following token.
        int syntax_error = 0;
        for (int i = 0; i < num_tokens; i++) {
            if ((strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) && (i + 1 >= num_tokens)) {
                fprintf(stderr, "Syntax error: expected filename after '%s'\n", tokens[i]);
                syntax_error = 1;
                break;
            }
        }
        if (syntax_error) {
            continue;
        }
        
        // Handle conditionals: "and" and "or".
        if (strcmp(tokens[0], "and") == 0) {
            for (int i = 0; i < num_tokens - 1; i++) {
                tokens[i] = tokens[i+1];
            }
            num_tokens--;
            if (prev_exit_status != 0) {
                continue;
            }
        } else if (strcmp(tokens[0], "or") == 0) {
            for (int i = 0; i < num_tokens - 1; i++) {
                tokens[i] = tokens[i+1];
            }
            num_tokens--;
            if (prev_exit_status == 0) {
                continue;
            }
        }
        
        // Look for pipeline operator.
        int pipe_index = -1;
        for (int i = 0; i < num_tokens; i++) {
            if (strcmp(tokens[i], "|") == 0) {
                pipe_index = i;
                break;
            }
        }
        
        // Prepare command structures.
        command cmd1;
        command cmd2;
        memset(&cmd1, 0, sizeof(command));
        memset(&cmd2, 0, sizeof(command));
        
        // Process tokens for cmd1 (up to pipe or end).
        int arg_index = 0;
        int limit = (pipe_index != -1) ? pipe_index : num_tokens;
        for (int i = 0; i < limit; i++) {
            if (strcmp(tokens[i], "<") == 0 && i+1 < limit) {
                cmd1.input_file = tokens[++i];
            } else if (strcmp(tokens[i], ">") == 0 && i+1 < limit) {
                cmd1.output_file = tokens[++i];
            } else {
                if (strchr(tokens[i], '*') != NULL) {
                    char *expanded[100];
                    int num_expanded = expand_token(tokens[i], expanded, 100);
                    for (int j = 0; j < num_expanded && arg_index < MAX_ARGS - 1; j++) {
                        cmd1.args[arg_index++] = expanded[j];
                    }
                } else {
                    cmd1.args[arg_index++] = tokens[i];
                }
            }
        }
        cmd1.args[arg_index] = NULL;
        
        // Process tokens for cmd2 if pipeline exists.
        if (pipe_index != -1) {
            arg_index = 0;
            for (int i = pipe_index+1; i < num_tokens; i++) {
                if (strcmp(tokens[i], "<") == 0 && i+1 < num_tokens) {
                    cmd2.input_file = tokens[++i];
                } else if (strcmp(tokens[i], ">") == 0 && i+1 < num_tokens) {
                    cmd2.output_file = tokens[++i];
                } else {
                    if (strchr(tokens[i], '*') != NULL) {
                        char *expanded[100];
                        int num_expanded = expand_token(tokens[i], expanded, 100);
                        for (int j = 0; j < num_expanded && arg_index < MAX_ARGS - 1; j++) {
                            cmd2.args[arg_index++] = expanded[j];
                        }
                    } else {
                        cmd2.args[arg_index++] = tokens[i];
                    }
                }
            }
            cmd2.args[arg_index] = NULL;
        }
        
        if (cmd1.args[0] == NULL) {
            continue;
        }
        
        /* Handle built-ins for non-pipeline commands. */
        if (pipe_index == -1) {
            if (strcmp(cmd1.args[0], "cd") == 0) {
                if (cmd1.args[1] == NULL || cmd1.args[2] != NULL) {
                    fprintf(stderr, "cd: requires one argument\n");
                    prev_exit_status = 1;
                } else {
                    if (chdir(cmd1.args[1]) != 0) {
                        perror("cd");
                        prev_exit_status = 1;
                    } else {
                        prev_exit_status = 0;
                    }
                }
                continue;
            }
            if (strcmp(cmd1.args[0], "pwd") == 0) {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    perror("pwd");
                    prev_exit_status = 1;
                } else {
                    printf("%s\n", cwd);
                    prev_exit_status = 0;
                }
                continue;
            }
            if (strcmp(cmd1.args[0], "which") == 0) {
                if (cmd1.args[1] == NULL || cmd1.args[2] != NULL) {
                    fprintf(stderr, "which: requires one argument\n");
                    prev_exit_status = 1;
                } else {
                    if (strcmp(cmd1.args[1], "cd") == 0 ||
                        strcmp(cmd1.args[1], "pwd") == 0 ||
                        strcmp(cmd1.args[1], "which") == 0 ||
                        strcmp(cmd1.args[1], "exit") == 0 ||
                        strcmp(cmd1.args[1], "die") == 0) {
                        fprintf(stderr, "which: %s is a shell built-in\n", cmd1.args[1]);
                        prev_exit_status = 1;
                    } else {
                        char *path = find_executable(cmd1.args[1]);
                        if (path) {
                            printf("%s\n", path);
                            free(path);
                            prev_exit_status = 0;
                        } else {
                            fprintf(stderr, "which: %s not found\n", cmd1.args[1]);
                            prev_exit_status = 1;
                        }
                    }
                }
                continue;
            }
            if (strcmp(cmd1.args[0], "exit") == 0) {
                if (interactive)
                    printf("Exiting my shell, goodbye!\n");
                exit(EXIT_SUCCESS);
            }
            if (strcmp(cmd1.args[0], "die") == 0) {
                for (int i = 1; cmd1.args[i] != NULL; i++) {
                    fprintf(stderr, "%s ", cmd1.args[i]);
                }
                fprintf(stderr, "\n");
                exit(EXIT_FAILURE);
            }
        }
        
        /* Special case: pipeline with built-in exit/die as second command.
           For example, "echo hello | exit" should run echo hello, then exit. */
        int status = 0;
        if (pipe_index != -1 && cmd2.args[0] != NULL &&
            (strcmp(cmd2.args[0], "exit") == 0 || strcmp(cmd2.args[0], "die") == 0)) {
            status = execute_command(&cmd1);
            prev_exit_status = status;
            if (strcmp(cmd2.args[0], "exit") == 0) {
                if (interactive) {
                    printf("Exiting my shell, goodbye!\n");
                }
                exit(EXIT_SUCCESS);
            } else {
                for (int i = 1; cmd2.args[i] != NULL; i++) {
                    fprintf(stderr, "%s ", cmd2.args[i]);
                }
                fprintf(stderr, "\n");
                exit(EXIT_FAILURE);
            }
        } else {
            if (pipe_index != -1) {
                status = execute_pipeline(&cmd1, &cmd2);
            } else {
                status = execute_command(&cmd1);
            }
            prev_exit_status = status;
        }
        
        // Optionally, print an extra newline for "cat" with input redirection.
        int print_newline = 0;
        if (strcmp(cmd1.args[0], "cat") == 0 && cmd1.input_file != NULL) {
            print_newline = 1;
        }
        if (interactive && print_newline) {
            printf("\n");
        }
    }
    
    if (interactive) {
        printf("Exiting my shell, goodbye!\n");
    }
}

// Remove the trailing newline from the string.
void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len-1] == '\n') {
        str[len-1] = '\0';
    }
}

/*
 * Split a command line into tokens using whitespace.
 * If a token starts with '#' the rest of the line is ignored.
 */
void tokenize_line(char *line, char *tokens[], int *num_tokens) {
    char *token = strtok(line, " \t");
    *num_tokens = 0;
    while (token != NULL && *num_tokens < MAX_TOKENS - 1) {
        if (token[0] == '#') {
            break;
        }
        tokens[(*num_tokens)++] = token;
        token = strtok(NULL, " \t");
    }
    tokens[*num_tokens] = NULL;
}

/*
 * If cmd contains a '/', return a duplicate of cmd.
 * Otherwise, search the directories in search_dirs for an executable.
 */
char *find_executable(const char *cmd) {
    if (strchr(cmd, '/') != NULL) {
        return strdup(cmd);
    }
    for (int i = 0; i < num_search_dirs; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", search_dirs[i], cmd);
        if (access(path, X_OK) == 0) {
            return strdup(path);
        }
    }
    return NULL;
}

/*
 * Fork a child process to execute a single command.
 * Sets up redirection as needed then calls execv.
 */
int execute_command(command *cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        if (cmd->input_file) {
            int fd_in = open(cmd->input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("open input_file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (cmd->output_file) {
            int fd_out = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (fd_out < 0) {
                perror("open output_file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        if (strchr(cmd->args[0], '/') == NULL) {
            char *path = find_executable(cmd->args[0]);
            if (!path) {
                fprintf(stderr, "Command not found: %s\n", cmd->args[0]);
                exit(EXIT_FAILURE);
            }
            cmd->args[0] = path;
        }
        execv(cmd->args[0], cmd->args);
        perror("execv");
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            return 1;
        }
    }
}

/*
 * Create a pipeline between two commands.
 * The first child's stdout is connected to the pipe's write end.
 * The second child's stdin is connected to the pipe's read end.
 */
int execute_pipeline(command *cmd1, command *cmd2) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }
    
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return 1;
    }
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (cmd1->input_file) {
            int fd_in = open(cmd1->input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("open input_file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (strchr(cmd1->args[0], '/') == NULL) {
            char *path = find_executable(cmd1->args[0]);
            if (!path) {
                fprintf(stderr, "Command not found: %s\n", cmd1->args[0]);
                exit(EXIT_FAILURE);
            }
            cmd1->args[0] = path;
        }
        execv(cmd1->args[0], cmd1->args);
        perror("execv");
        exit(EXIT_FAILURE);
    }
    
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        return 1;
    }
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (cmd2->output_file) {
            int fd_out = open(cmd2->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (fd_out < 0) {
                perror("open output_file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        if (strchr(cmd2->args[0], '/') == NULL) {
            char *path = find_executable(cmd2->args[0]);
            if (!path) {
                fprintf(stderr, "Command not found: %s\n", cmd2->args[0]);
                exit(EXIT_FAILURE);
            }
            cmd2->args[0] = path;
        }
        execv(cmd2->args[0], cmd2->args);
        perror("execv");
        exit(EXIT_FAILURE);
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    int status;
    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 1;
    }
}
