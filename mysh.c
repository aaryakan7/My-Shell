#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>

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
    char *owned_args[MAX_ARGS];
    int owned_count;
} command;

// Function Prototypes
char *find_executable(const char *cmd);
int execute_command(command *cmd);
int execute_pipeline(command *cmd1, command *cmd2);
void run_shell(FILE *input_stream, int interactive);
void trim_newline(char *str);
void tokenize_line(char *line, char *tokens[], int *num_tokens);
int expand_token(const char *token, char *expanded_tokens[], int max_expanded);
static int read_command_line(int fd, char *line, size_t size);
static int append_arg(command *cmd, char *arg, int *index, int owned);
static int close_child_stdin = 0;
static int redirect_builtin(const command *cmd, int saved[2]);
static void restore_builtin(int saved[2]);
static void free_command(command *cmd);
static int run_builtin(command *cmd);
static int is_builtin(const char *name);

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
    int has_previous_command = 0;
    int input_fd = fileno(input_stream);
    close_child_stdin = !isatty(input_fd);
    
    if (interactive) {
        printf("Welcome to my shell!\n");
    }
    
    while (1) {
        if (interactive) {
            printf("mysh> ");
            fflush(stdout);
        }
        
        int read_result = read_command_line(input_fd, line, sizeof(line));
        if (read_result == -2) {
            fprintf(stderr, "Syntax error: command line too long\n");
            prev_exit_status = 1;
            has_previous_command = 1;
            continue;
        }
        if (read_result <= 0) {
            break;
        }
        
        trim_newline(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        char *tokens[MAX_TOKENS];
        int num_tokens = 0;
        tokenize_line(line, tokens, &num_tokens);
        if (num_tokens == MAX_TOKENS) {
            fprintf(stderr, "Syntax error: too many tokens\n");
            prev_exit_status = 1;
            has_previous_command = 1;
            continue;
        }
        if (num_tokens == 0) {
            continue;
        }

        int conditional = strcmp(tokens[0], "and") == 0 || strcmp(tokens[0], "or") == 0;
        if (conditional && !has_previous_command) {
            fprintf(stderr, "Syntax error: conditional cannot start the input\n");
            prev_exit_status = 1;
            has_previous_command = 1;
            continue;
        }
        has_previous_command = 1;
        
        // Check syntax before constructing commands.
        int syntax_error = 0;
        for (int i = 0; i < num_tokens; i++) {
            if (strcmp(tokens[i], "|") == 0 && (i == 0 || i == num_tokens - 1 || (i + 1 < num_tokens && strcmp(tokens[i + 1], "|") == 0))) {
                fprintf(stderr, "Syntax error near '|\n");
                syntax_error = 1;
                break;
            }
            if ((strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) &&
                (i + 1 >= num_tokens || strcmp(tokens[i + 1], "<") == 0 || strcmp(tokens[i + 1], ">") == 0 || strcmp(tokens[i + 1], "|") == 0)) {
                fprintf(stderr, "Syntax error: expected filename after '%s'\n", tokens[i]);
                syntax_error = 1;
                break;
            }
        }
        if (syntax_error) {
            prev_exit_status = 1;
            continue;
        }
        
        // Handle conditionals: "and" and "or".
        if (strcmp(tokens[0], "and") == 0) {
            if (num_tokens == 1) {
                fprintf(stderr, "Syntax error: expected command after 'and'\n");
                prev_exit_status = 1;
                continue;
            }
            for (int i = 0; i < num_tokens - 1; i++) {
                tokens[i] = tokens[i+1];
            }
            num_tokens--;
            if (prev_exit_status != 0) {
                continue;
            }
        } else if (strcmp(tokens[0], "or") == 0) {
            if (num_tokens == 1) {
                fprintf(stderr, "Syntax error: expected command after 'or'\n");
                prev_exit_status = 1;
                continue;
            }
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
                if (pipe_index != -1) {
                    syntax_error = 1;
                    break;
                }
                pipe_index = i;
            }
        }
        if (syntax_error) { prev_exit_status = 1; continue; }
        
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
                    for (int j = 0; j < num_expanded; j++) {
                        if (!append_arg(&cmd1, expanded[j], &arg_index, 1)) {
                            for (int k = j; k < num_expanded; k++) free(expanded[k]);
                            syntax_error = 1;
                            break;
                        }
                    }
                } else {
                    if (!append_arg(&cmd1, tokens[i], &arg_index, 0)) syntax_error = 1;
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
                        for (int j = 0; j < num_expanded; j++) {
                            if (!append_arg(&cmd2, expanded[j], &arg_index, 1)) {
                                for (int k = j; k < num_expanded; k++) free(expanded[k]);
                                syntax_error = 1;
                                break;
                            }
                        }
                    } else {
                        if (!append_arg(&cmd2, tokens[i], &arg_index, 0)) syntax_error = 1;
                    }
                }
            }
            cmd2.args[arg_index] = NULL;
        }
        if (syntax_error) {
            fprintf(stderr, "Syntax error: too many arguments\n");
            prev_exit_status = 1;
            free_command(&cmd1);
            free_command(&cmd2);
            continue;
        }
        
        if (cmd1.args[0] == NULL) {
            free_command(&cmd1);
            free_command(&cmd2);
            continue;
        }
        
        int status = 0;
        if (pipe_index == -1 && is_builtin(cmd1.args[0])) {
            int saved[2] = {-1, -1};
            if (redirect_builtin(&cmd1, saved) != 0) {
                prev_exit_status = 1;
            } else {
                status = run_builtin(&cmd1);
                fflush(stdout);
                if (status == 100) {
                    if (interactive) printf("Exiting my shell, goodbye!\n");
                    restore_builtin(saved);
                    free_command(&cmd1);
                    exit(EXIT_SUCCESS);
                }
                if (status == 101) {
                    restore_builtin(saved);
                    free_command(&cmd1);
                    exit(EXIT_FAILURE);
                }
                restore_builtin(saved);
                prev_exit_status = status;
            }
        } else {
            if (pipe_index != -1) {
                status = execute_pipeline(&cmd1, &cmd2);
                if (strcmp(cmd1.args[0], "exit") == 0 || strcmp(cmd2.args[0], "exit") == 0) {
                    if (interactive) printf("Exiting my shell, goodbye!\n");
                    free_command(&cmd1);
                    free_command(&cmd2);
                    exit(EXIT_SUCCESS);
                }
                if (strcmp(cmd1.args[0], "die") == 0 || strcmp(cmd2.args[0], "die") == 0) {
                    free_command(&cmd1);
                    free_command(&cmd2);
                    exit(EXIT_FAILURE);
                }
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
        free_command(&cmd1);
        free_command(&cmd2);
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

static int read_command_line(int fd, char *line, size_t size) {
    size_t used = 0;
    char ch;
    int too_long = 0;
    while (used + 1 < size) {
        ssize_t n = read(fd, &ch, 1);
        if (n == 0) {
            if (used == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ch == '\n') break;
        line[used++] = ch;
    }
    if (used + 1 == size) {
        while (1) {
            ssize_t n = read(fd, &ch, 1);
            if (n == 0) break;
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (ch == '\n') break;
            too_long = 1;
        }
    }
    line[used] = '\0';
    return too_long ? -2 : 1;
}

static int append_arg(command *cmd, char *arg, int *index, int owned) {
    if (*index >= MAX_ARGS - 1) return 0;
    cmd->args[(*index)++] = arg;
    cmd->args[*index] = NULL;
    if (owned) cmd->owned_args[cmd->owned_count++] = arg;
    return 1;
}

static int redirect_builtin(const command *cmd, int saved[2]) {
    fflush(stdout);
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { perror("open input_file"); return 1; }
        saved[0] = dup(STDIN_FILENO);
        if (saved[0] < 0 || dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            if (saved[0] >= 0) close(saved[0]);
            saved[0] = -1;
            return 1;
        }
        close(fd);
    }
    if (cmd->output_file) {
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (fd < 0) { perror("open output_file"); restore_builtin(saved); return 1; }
        saved[1] = dup(STDOUT_FILENO);
        if (saved[1] < 0 || dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            if (saved[1] >= 0) close(saved[1]);
            saved[1] = -1;
            restore_builtin(saved);
            return 1;
        }
        close(fd);
    }
    return 0;
}

static void restore_builtin(int saved[2]) {
    fflush(stdout);
    if (saved[0] >= 0) { dup2(saved[0], STDIN_FILENO); close(saved[0]); saved[0] = -1; }
    if (saved[1] >= 0) { dup2(saved[1], STDOUT_FILENO); close(saved[1]); saved[1] = -1; }
}

static void free_command(command *cmd) {
    for (int i = 0; i < cmd->owned_count; i++) free(cmd->owned_args[i]);
    cmd->owned_count = 0;
}

static int is_builtin(const char *name) {
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 ||
           strcmp(name, "which") == 0 || strcmp(name, "exit") == 0 ||
           strcmp(name, "die") == 0;
}

static int run_builtin(command *cmd) {
    char **args = cmd->args;
    if (strcmp(args[0], "cd") == 0) {
        if (!args[1] || args[2]) { fprintf(stderr, "cd: requires one argument\n"); return 1; }
        if (chdir(args[1]) < 0) { perror("cd"); return 1; }
        return 0;
    }
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[1024];
        if (args[1]) { fprintf(stderr, "pwd: takes no arguments\n"); return 1; }
        if (!getcwd(cwd, sizeof(cwd))) { perror("pwd"); return 1; }
        puts(cwd);
        return 0;
    }
    if (strcmp(args[0], "which") == 0) {
        if (!args[1] || args[2] || is_builtin(args[1])) return 1;
        char *path = find_executable(args[1]);
        if (!path) return 1;
        puts(path);
        free(path);
        return 0;
    }
    if (strcmp(args[0], "exit") == 0) return 100;
    for (int i = 1; args[i]; i++) fprintf(stderr, "%s%s", i == 1 ? "" : " ", args[i]);
    fputc('\n', stderr);
    return 101;
}

/*
 * Split a command line into tokens using whitespace.
 */
void tokenize_line(char *line, char *tokens[], int *num_tokens) {
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';
    char *token = strtok(line, " \t");
    *num_tokens = 0;
    while (token != NULL) {
        if (*num_tokens >= MAX_TOKENS - 1) {
            *num_tokens = MAX_TOKENS;
            tokens[MAX_TOKENS - 1] = NULL;
            return;
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
        if (close_child_stdin && !cmd->input_file) close(STDIN_FILENO);
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
        if (is_builtin(cmd->args[0])) {
            int status = run_builtin(cmd);
            fflush(NULL);
            _exit(status == 100 ? 0 : status == 101 ? 1 : status);
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
    fflush(NULL);
    
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return 1;
    }
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (close_child_stdin && !cmd1->input_file) close(STDIN_FILENO);
        if (cmd1->input_file) {
            int fd_in = open(cmd1->input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("open input_file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (cmd1->output_file) {
            int fd_out = open(cmd1->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (fd_out < 0) { perror("open output_file"); _exit(EXIT_FAILURE); }
            if (dup2(fd_out, STDOUT_FILENO) < 0) { perror("dup2"); _exit(EXIT_FAILURE); }
            close(fd_out);
        }
        if (is_builtin(cmd1->args[0])) {
            int result = run_builtin(cmd1);
            fflush(NULL);
            _exit(result == 100 ? 0 : result == 101 ? 1 : result);
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
        if (cmd2->input_file) {
            int fd_in = open(cmd2->input_file, O_RDONLY);
            if (fd_in < 0) { perror("open input_file"); _exit(EXIT_FAILURE); }
            if (dup2(fd_in, STDIN_FILENO) < 0) { perror("dup2"); _exit(EXIT_FAILURE); }
            close(fd_in);
        }
        if (is_builtin(cmd2->args[0])) {
            int result = run_builtin(cmd2);
            fflush(NULL);
            _exit(result == 100 ? 0 : result == 101 ? 1 : result);
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
