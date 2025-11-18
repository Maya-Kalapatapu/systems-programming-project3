

#include "mysh.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>



bool is_interactive = false;
bool reading_from_terminal = false;
int last_exit_status = 0; 
int shell_exit_status = EXIT_SUCCESS; 
static bool have_seen_command = false; // Track whether we've seen any (non-empty, syntactically valid) command yet.


// Custom error message printing
void print_mysh_error(const char* context, const char* message) {
    fprintf(stderr, "mysh: %s: %s\n", context, message);
}

//This function will be used for robust string duplication
static char* safe_strdup(const char* s) {
    if (!s) return NULL;
    char* d = (char*)malloc(strlen(s) + 1);
    if (d == NULL) {
        print_mysh_error("malloc", "Failed to duplicate string (internal error)");
        // Since this is called during parsing, a return of NULL will be caught
        return NULL; 
    }
    strcpy(d, s);
    return d;
}

// Cleans up dynamically allocated memory in job_t
void free_job(job_t *job) {
    if (!job) return;

    if (job->argvv) {
        for (size_t i = 0; i < job->num_procs; i++) {
            if (job->argvv[i]) {
                // job->argvv[i] is char**
                char **argv = (char**)job->argvv[i];
                for (size_t j = 0; argv[j] != NULL; j++) {
                    free(argv[j]);
                }
                free(job->argvv[i]);
            }
        }
        free(job->argvv);
    }
    free(job->infile);
    free(job->outfile);
    
    // Reset job structure 
    memset(job, 0, sizeof(job_t));
}

// Custom tokenizer to handle special characters as single tokens
static int simple_tokenize(char* line, char* tokens[MAX_TOKENS]) {
    int t = 0;
    char* p = line;
    char* start = NULL;
    
    // handeling leading whitespace
    while (*p && isspace(*p)) p++;

    while (*p && t < MAX_TOKENS) {
        if (*p == '#') {
            break;
        }

        if (*p == '|' || *p == '<' || *p == '>') {
            // Special character found, save it as a token
            tokens[t++] = safe_strdup((char[]){ *p, '\0' });
            if (!tokens[t-1]) return -1;
            p++;
        } else if (isspace(*p)) {
            p++;
        } else { // regualar token initiated
            start = p;
            // Find end of the token (whitespace or special char)
            while (*p && !isspace(*p) && *p != '|' && *p != '<' && *p != '>') {
                p++;
            }
            // Null-terminate the token 
            char temp = *p;
            *p = '\0';
            tokens[t++] = safe_strdup(start);
            if (!tokens[t-1]) return -1;
            *p = temp; // Restore char
        }
        // Ignore internal whitespace
        while (*p && isspace(*p)) p++;
    }
    tokens[t] = NULL;
    return t;
}



// Parses the token stream into a job_t structure 
int parse_line(char *line, job_t *job) {
    char *tokens[MAX_TOKENS];
    char **temp_argvs[MAX_COMMANDS];
    int token_count = simple_tokenize(line, tokens);

    if (token_count <= 0) {
        // 0  => empty line / comment only  (not an error)
        // -1 => tokenization error
        return (token_count == 0) ? 0 : -1;
    }

    // Initialize temp argv pointers to NULL so cleanup is easy
    memset(temp_argvs, 0, sizeof(temp_argvs));

    int current_token   = 0;
    int current_cmd_idx = 0;
    int current_argc    = 0;

    // Initialize job
    job->cond      = COND_NONE;
    job->infile    = NULL;
    job->outfile   = NULL;
    job->num_procs = 0;
    job->argvv     = NULL;

    // Check for leading conditional
    if (strcmp(tokens[0], "and") == 0) {
        job->cond = COND_AND;
        current_token++;
    } else if (strcmp(tokens[0], "or") == 0) {
        job->cond = COND_OR;
        current_token++;
    }

    // If only conditional token remains, that's a syntax error
    if (current_token >= token_count) {
        if (job->cond != COND_NONE) {
            print_mysh_error("syntax error", "Conditional must be followed by a command");
            goto parse_error;
        }
        goto parse_cleanup_success;
    }

    // Initialize argument array for the first command
    temp_argvs[0] = (char **)malloc(MAX_ARGS * sizeof(char *));
    if (!temp_argvs[0]) {
        print_mysh_error("malloc", "Failed to allocate argument array");
        goto parse_error;
    }
    temp_argvs[0][0] = NULL;

    // Process tokens
    while (current_token < token_count) {
        char *token = tokens[current_token];

        // Handle pipeline separators
        if (strcmp(token, "|") == 0) {
            if (current_argc == 0) {
                print_mysh_error("syntax error", "Empty command before pipe '|'");
                goto parse_error;
            }
            if (current_cmd_idx >= MAX_COMMANDS - 1) {
                print_mysh_error("syntax error", "Too many commands in pipeline");
                goto parse_error;
            }

            // Finalize current command and move to next
            temp_argvs[current_cmd_idx][current_argc] = NULL;
            current_cmd_idx++;
            current_argc = 0;

            // Initialize next command's argument array
            temp_argvs[current_cmd_idx] = (char **)malloc(MAX_ARGS * sizeof(char *));
            if (!temp_argvs[current_cmd_idx]) {
                print_mysh_error("malloc", "Failed to allocate argument array");
                goto parse_error;
            }
            temp_argvs[current_cmd_idx][0] = NULL;

            current_token++;
            continue;
        }

        // Handle redirection tokens: '<' or '>'
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            if (current_token + 1 >= token_count) {
                print_mysh_error("syntax error", "Redirection requires a valid filename");
                goto parse_error;
            }

            char *filename = tokens[current_token + 1];

            if (strcmp(token, "<") == 0) {
                if (job->infile) {
                    print_mysh_error("syntax error", "Multiple input redirections");
                    goto parse_error;
                }
                job->infile = safe_strdup(filename);
                if (!job->infile) {
                    goto parse_error;
                }
            } else { // ">"
                if (job->outfile) {
                    print_mysh_error("syntax error", "Multiple output redirections");
                    goto parse_error;
                }
                job->outfile = safe_strdup(filename);
                if (!job->outfile) {
                    goto parse_error;
                }
            }

            current_token += 2;  // skip redirection token and filename
            continue;
        }

        // Regular argument
        // Spec: "Use of and or or after a | is invalid."
        // That corresponds to a subcommand (current_cmd_idx > 0) whose
        // first token (current_argc == 0) is "and" or "or".
        if (current_argc == 0 && current_cmd_idx > 0 &&
            (strcmp(token, "and") == 0 || strcmp(token, "or") == 0)) {
            print_mysh_error("syntax error", "Conditionals may not appear after a pipe");
            goto parse_error;
        }

        if (current_argc >= MAX_ARGS - 1) {
            print_mysh_error("syntax error", "Too many arguments for command");
            goto parse_error;
        }

        temp_argvs[current_cmd_idx][current_argc] = safe_strdup(token);
        if (!temp_argvs[current_cmd_idx][current_argc]) {
            goto parse_error;
        }
        current_argc++;
        temp_argvs[current_cmd_idx][current_argc] = NULL;

        current_token++;
    }

    // Final check for valid last command
    if (current_argc == 0) {
        print_mysh_error("syntax error", "Empty command at end of line/pipeline");
        goto parse_error;
    }

    // Finalize the last command's arguments
    temp_argvs[current_cmd_idx][current_argc] = NULL;
    job->num_procs = current_cmd_idx + 1;

    // Allocate job->argvv and transfer ownership of temp_argvs
    job->argvv = (char ***)malloc(job->num_procs * sizeof(char **));
    if (!job->argvv) {
        print_mysh_error("malloc", "Failed to allocate process array");
        goto parse_error;
    }

    for (size_t i = 0; i < job->num_procs; i++) {
        job->argvv[i] = temp_argvs[i];
        temp_argvs[i] = NULL;
    }

parse_cleanup_success:
    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }
    return 1;

parse_error:
    // Cleanup any temp_argvs and their strings
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (temp_argvs[i]) {
            char **argv = temp_argvs[i];
            for (int j = 0; j < MAX_ARGS; j++) {
                if (argv[j] == NULL) break;
                free(argv[j]);
            }
            free(temp_argvs[i]);
        }
    }

    free(job->infile);
    free(job->outfile);
    job->infile  = NULL;
    job->outfile = NULL;

    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }

    memset(job, 0, sizeof(job_t));
    return -1;
}

    

int read_and_execute_loop(int fd) {
    char buffer[INPUT_BUFFER_SIZE];
    ssize_t bytes_read = 0;
    size_t line_start = 0; 
    
    while (true) {
        // Print prompt in interactive mode
        if (is_interactive) {
            write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
        }

        // Read more data into the buffer using read()
        ssize_t n = read(fd, buffer + bytes_read, INPUT_BUFFER_SIZE - bytes_read - 1);

        if (n == 0) {
            // End of input stream (EOF)
            break; 
        }
        if (n < 0) {
            if (errno == EINTR) continue; 
            print_mysh_error("read", "Error reading input");
            break;
        }
        bytes_read += n;
        buffer[bytes_read] = '\0';

        // Scan the buffer for a newline to find a complete command
        size_t i = line_start;
        while (i < bytes_read) {
            if (buffer[i] == '\n') {
                buffer[i] = '\0'; // Null-terminate the command
                
                // Process command (from line_start to i)
                job_t job = {0};
                int parse_status = parse_line(buffer + line_start, &job);

                if (parse_status > 0) {

                    // Enforce: conditionals should not occur in the first command.
                    if (!have_seen_command && job.cond != COND_NONE) {
                        print_mysh_error("syntax error",
                                         "Conditional cannot appear on first command");
                        last_exit_status = 1;
                        free_job(&job);
                    } else {
                        // Conditional logic check
                        if (job.cond == COND_AND && last_exit_status != 0) {
                            // Skip execution; preserve last_exit_status.
                        } else if (job.cond == COND_OR && last_exit_status == 0) {
                            // Skip execution; preserve last_exit_status.
                        } else {
                            // Execute the job
                            int cmd_status = 0;
                            exec_action_t action =
                                execute_job(&job, reading_from_terminal, &cmd_status);
                            last_exit_status = cmd_status;

                            // Check if a built-in command ('exit' or 'die') requested termination
                            if (action == EXEC_EXIT) {
                                free_job(&job);
                                shell_exit_status = EXIT_SUCCESS;
                                return shell_exit_status;
                            } else if (action == EXEC_DIE) {
                                free_job(&job);
                                shell_exit_status = EXIT_FAILURE;
                                return shell_exit_status;
                            }
                        }

                        // We saw a syntactically valid command this line,
                        // whether or not it was executed due to conditionals.
                        have_seen_command = true;
                        free_job(&job);
                    }
                } else if (parse_status == -1) {
                    // Syntax error
                    last_exit_status = 1;
                }
                
                // getting ready for the next command
                line_start = i + 1;
            }
            i++;
        }
        
        // Shift remaining data to the front if necessary
        if (line_start > 0) {
            bytes_read -= line_start;
            memmove(buffer, buffer + line_start, bytes_read);
            line_start = 0;
        }
    }
    return shell_exit_status;
}

#ifndef TESTING

// Main function (Entry Point)
int main(int argc, char *argv[]) {
    int input_fd = STDIN_FILENO;

    if (argc > 2) {
        write(STDERR_FILENO, "Usage: mysh [scriptfile]\n", 25);
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        // Batch mode: read from file
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            print_mysh_error("mysh", "Unable to open input file");
            return EXIT_FAILURE;
        }
        reading_from_terminal = false;
    } else {
        // Interactive or Batch mode: read from STDIN
        reading_from_terminal = isatty(STDIN_FILENO);
    }
    
    // Determine interactive status
    is_interactive = reading_from_terminal;

    if (is_interactive) {
        write(STDOUT_FILENO, "Welcome to my shell!\n", 21);
    }

    // Start the read/execute loop
    int exit_code = read_and_execute_loop(input_fd);
    
    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    if (is_interactive) {
        write(STDOUT_FILENO, "Exiting my shell.\n", 18);
    }

    return exit_code;
}
#endif
