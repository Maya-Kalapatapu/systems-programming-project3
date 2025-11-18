

#include "mysh.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

// --- Global Variable Definitions ---
bool is_interactive = false;
bool reading_from_terminal = false;
int last_exit_status = 0; 
int shell_exit_status = EXIT_SUCCESS; 

// --- Helper Functions ---

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
void free_job_t(job_t *job) {
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
    
    // Reset job structure (optional, but good practice)
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
int parse_line(char* line, job_t *job) {
    char* tokens[MAX_TOKENS];
    char** temp_argvs[MAX_COMMANDS];
    int token_count = simple_tokenize(line, tokens);
    
    if (token_count <= 0) {
        return token_count == 0 ? 0 : -1; 
    }

    int current_token = 0;
    int current_cmd_idx = 0;
    int current_argc = 0;
    
    job->conditional = COND_NONE;
    job->infile = NULL;
    job->outfile = NULL;
    job->num_procs = 0;
    
    // Check for conditional commands
    if (strcmp(tokens[0], "and") == 0) {
        job->conditional = COND_AND;
        current_token++;
    } else if (strcmp(tokens[0], "or") == 0) {
        job->conditional = COND_OR;
        current_token++;
    }
    
    // If only conditional token remains
    if (current_token >= token_count) {
        if (job->conditional != COND_NONE) {
            print_mysh_error("syntax error", "Conditional must be followed by a command");
            goto parse_error;
        }
        goto parse_cleanup_success;
    }

    // Initialize argument array for the first command
    temp_argvs[0] = (char**)malloc(MAX_ARGS * sizeof(char*));
    if (!temp_argvs[0]) {
        print_mysh_error("malloc", "Failed to allocate argument array");
        goto parse_error;
    }
    temp_argvs[0][0] = NULL; // Initialize first slot for safety

    // Process tokens
    while (current_token < token_count) {
        char* token = tokens[current_token];

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
            temp_argvs[current_cmd_idx] = (char**)malloc(MAX_ARGS * sizeof(char*));
            if (!temp_argvs[current_cmd_idx]) {
                print_mysh_error("malloc", "Failed to allocate argument array");
                goto parse_error;
            }

            current_token++;
            continue;
        }

        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            // Redirection
            if (current_token + 1 >= token_count || is_builtin_name(tokens[current_token + 1])) {
                print_mysh_error("syntax error", "Redirection requires a valid filename");
                goto parse_error;
            }
            
            char* filename = tokens[current_token + 1];
            
            // Redirection files are stored at the job level (first/last process)
            if (strcmp(token, "<") == 0) {
                if (job->infile) {
                    print_mysh_error("syntax error", "Multiple input redirections");
                    goto parse_error;
                }
                job->infile = safe_strdup(filename);
            } else { 
                if (job->outfile) {
                    print_mysh_error("syntax error", "Multiple output redirections");
                    goto parse_error;
                }
                job->outfile = safe_strdup(filename);
            }
            
            current_token += 2; // Skip < or > and the filename
            continue;
        }

        // Regular argument
        if (current_argc >= MAX_ARGS - 1) { 
            print_mysh_error("syntax error", "Too many arguments for command");
            goto parse_error;
        }
        temp_argvs[current_cmd_idx][current_argc++] = safe_strdup(token);
        current_token++;
    }
    
    // Final check for valid command
    if (current_argc == 0) {
        print_mysh_error("syntax error", "Empty command at end of line/pipeline");
        goto parse_error;
    }
    
    // Finalize the last command's arguments
    temp_argvs[current_cmd_idx][current_argc] = NULL;
    job->num_procs = current_cmd_idx + 1;

    // Transfer temp_argvs to the job->argvv structure
    job->argvv = (char***)malloc(job->num_procs * sizeof(char**));
    if (!job->argvv) {
        print_mysh_error("malloc", "Failed to allocate process array");
        goto parse_error;
    }

    for (size_t i = 0; i < job->num_procs; i++) {
        job->argvv[i] = temp_argvs[i];
    }
    
parse_cleanup_success:
    // free the temporary token array (as tokens were duplicated)
    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }
    return 1; 

parse_error:
    // Manual cleanup on error
    for (int i = 0; i <= current_cmd_idx; i++) {
        if (i > 0 && temp_argvs[i] == NULL) break; // Reached non-initialized
        if (temp_argvs[i]) {
            for (int j = 0; j < MAX_ARGS; j++) {
                if (j > 0 && temp_argvs[i][j] == NULL) break;
                free(temp_argvs[i][j]);
            }
            free(temp_argvs[i]);
        }
    }
    free(job->infile);
    free(job->outfile);
    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }
    memset(job, 0, sizeof(job_t)); // Clear job state
    return -1;
}

// The main input reading loop for both interactive and batch mode
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
                    // Command successfully parsed

                    // Conditional logic check
                    if (job.conditional == COND_AND && last_exit_status != 0) {
                        last_exit_status = last_exit_status; // Preserve previous failure status
                    } else if (job.conditional == COND_OR && last_exit_status == 0) {
                        last_exit_status = 0; // Preserve previous success status
                    } else {
                        // Execute the job
                        int cmd_status = 0;
                        exec_action_t action = execute_job(&job, reading_from_terminal, &cmd_status);
                        last_exit_status = cmd_status;

                        // Check if a built-in command ('exit' or 'die') requested termination
                        if (action == EXEC_EXIT) {
                            free_job_t(&job);
                            return shell_exit_status; // Exit gracefully
                        } else if (action == EXEC_DIE) {
                            free_job_t(&job);
                            return shell_exit_status; // Exit abnormally (status 1)
                        }
                    }
                    free_job_t(&job);
                } else if (parse_status == -1) {
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
