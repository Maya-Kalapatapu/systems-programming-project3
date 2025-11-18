// Command execution and built-in commands for mysh.
//
// This file is responsible for:
//   - Executing parsed jobs (simple commands + pipelines)
//   - Handling input/output redirection
//   - Handling /dev/null behavior for non-tty input
//   - Implementing built-in commands: cd, pwd, which, exit, die
//
// Parsing, the main input loop, and conditionals belong in mysh_core.c.

#include "mysh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* Minimal strdup helper (avoids relying on non-standard strdup). */
static char *my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) {
        memcpy(p, s, len);
    }
    return p;
}

static int  run_simple_command(const job_t *job, bool input_is_tty);
static int  run_pipeline(const job_t *job, bool input_is_tty);

static int  setup_redirection(const char *infile,
                              const char *outfile,
                              bool input_is_tty);

static int  setup_stdin_for_batch(bool input_is_tty);

static int  open_input_file(const char *path);
static int  open_output_file(const char *path);

static int  is_builtin(const char *name);
static int  run_builtin_parent(char *const argv[], int *status_out,
                               exec_action_t *action_out);
static int  run_builtin_child(char *const argv[]);  // builtins when used in pipelines

static int  builtin_cd(char *const argv[]);
static int  builtin_pwd(char *const argv[]);
static int  builtin_which(char *const argv[]);
static int  builtin_exit(char *const argv[], exec_action_t *action_out);
static int  builtin_die(char *const argv[], exec_action_t *action_out);

static char *resolve_program_path(const char *cmd_name);

// Public entry point for executing a single parsed job.
exec_action_t
execute_job(const job_t *job, bool input_is_tty, int *cmd_status)
{
    exec_action_t action = EXEC_CONTINUE;

    if (cmd_status != NULL) {
        *cmd_status = 1;  // default to failure until command runs
    }

    if (job == NULL || job->num_procs == 0 || job->argvv == NULL) {
        // Nothing to do; treat as success.
        if (cmd_status != NULL) {
            *cmd_status = 0;
        }
        return EXEC_CONTINUE;
    }

    // Scan for exit/die anywhere in the job so we can honor
    // "jobs involving exit/die terminate the shell" even in pipelines.
    bool has_exit = false;
    bool has_die  = false;
    for (size_t i = 0; i < job->num_procs; i++) {
        if (job->argvv[i] == NULL || job->argvv[i][0] == NULL) {
            continue;
        }
        const char *cmd = job->argvv[i][0];
        if (strcmp(cmd, "die") == 0) {
            has_die = true;
        } else if (strcmp(cmd, "exit") == 0) {
            has_exit = true;
        }
    }

    // Special handling for a single built-in command in the parent process
    // so that cd/exit/die affect the shell itself. We also honor
    // redirection (<, >) for these built-ins.
    if (job->num_procs == 1 &&
        job->argvv[0] != NULL &&
        job->argvv[0][0] != NULL &&
        is_builtin(job->argvv[0][0])) {

        int status = 1;
        exec_action_t builtin_action = EXEC_CONTINUE;

        int saved_stdin  = -1;
        int saved_stdout = -1;
        int redir_error  = 0;

        // Apply redirection in the parent if requested.
        // Save fds so we can restore them after running the builtin.
        if (job->infile != NULL) {
            saved_stdin = dup(STDIN_FILENO);
            if (saved_stdin < 0) {
                perror("dup");
                redir_error = -1;
            }
        }
        if (redir_error == 0 && job->outfile != NULL) {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0) {
                perror("dup");
                redir_error = -1;
            }
        }

        if (redir_error == 0) {
            // Set up stdin from infile if present
            if (job->infile != NULL) {
                int fd_in = open_input_file(job->infile);
                if (fd_in < 0) {
                    redir_error = -1;
                } else {
                    if (dup2(fd_in, STDIN_FILENO) < 0) {
                        perror("dup2");
                        close(fd_in);
                        redir_error = -1;
                    } else {
                        close(fd_in);
                    }
                }
            }

            // Set up stdout to outfile if present
            if (redir_error == 0 && job->outfile != NULL) {
                int fd_out = open_output_file(job->outfile);
                if (fd_out < 0) {
                    redir_error = -1;
                } else {
                    if (dup2(fd_out, STDOUT_FILENO) < 0) {
                        perror("dup2");
                        close(fd_out);
                        redir_error = -1;
                    } else {
                        close(fd_out);
                    }
                }
            }
        }

        if (redir_error == 0) {
            if (run_builtin_parent(job->argvv[0], &status, &builtin_action) < 0) {
                status = 1;
            }
        } else {
            status = 1;
        }

        // Restore original stdin/stdout if we changed them
        if (saved_stdin != -1) {
            if (dup2(saved_stdin, STDIN_FILENO) < 0) {
                perror("dup2");
            }
            close(saved_stdin);
        }
        if (saved_stdout != -1) {
            if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
                perror("dup2");
            }
            close(saved_stdout);
        }

        if (cmd_status != NULL) {
            *cmd_status = status;
        }

        // If the builtin itself requested EXIT/DIE, honor that.
        if (builtin_action != EXEC_CONTINUE) {
            return builtin_action;
        }

        // Otherwise, if this job involved exit/die (e.g., an "exit" that
        // failed somehow), still treat it as a terminating job according
        // to the scan above.
        if (action == EXEC_CONTINUE) {
            if (has_die) {
                action = EXEC_DIE;
            } else if (has_exit) {
                action = EXEC_EXIT;
            }
        }

        return action;
    }

    // Not a "special" built-in case; either:
    //   - a single external command
    //   - a builtin we run in a child (e.g., in a pipeline)
    //   - a pipeline of multiple commands
    int status = 1;

    if (job->num_procs == 1) {
        status = run_simple_command(job, input_is_tty);
    } else {
        status = run_pipeline(job, input_is_tty);
    }

    if (cmd_status != NULL) {
        *cmd_status = status;
    }

    // For non-parent-builtins, if this job involved die/exit anywhere,
    // tell the core loop to terminate after the job completes.
    if (action == EXEC_CONTINUE) {
        if (has_die) {
            action = EXEC_DIE;
        } else if (has_exit) {
            action = EXEC_EXIT;
        }
    }

    return action;
}


// Simple command execution (no pipelines).
static int
run_simple_command(const job_t *job, bool input_is_tty)
{
    // job->num_procs is assumed to be 1.
    char *const *argv = job->argvv[0];

    if (argv == NULL || argv[0] == NULL) {
        return 0; // treat empty as success
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // Child
        if (setup_redirection(job->infile, job->outfile, input_is_tty) < 0) {
            _exit(1);
        }

        // Builtin in a child (e.g., because of redirection decisions or tests)
        if (is_builtin(argv[0])) {
            (void)run_builtin_child((char *const *)argv);
            _exit(0);
        }

        // External command: resolve path and execv
        char *path = resolve_program_path(argv[0]);
        if (path == NULL) {
            fprintf(stderr, "%s: command not found\n", argv[0]);
            _exit(127);
        }

        execv(path, argv);
        perror("execv");
        free(path);
        _exit(127);
    }

    // Parent: wait for the child
    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
    } else {
        // Treat abnormal termination as failure
        return 1;
    }
}


// Pipeline execution
// For N processes, there are N-1 pipes. We:
//   - set up all pipes
//   - fork each child, wiring its stdin/stdout to the correct pipe ends
//   - handle builtin vs external for each stage
//   - close all pipes in the parent
//   - wait for all children, and return the exit status of the last process.
static int
run_pipeline(const job_t *job, bool input_is_tty)
{
    size_t n = job->num_procs;
    if (n < 2) {
        // Should not happen; fall back defensively
        return run_simple_command(job, input_is_tty);
    }

    int pipes[n - 1][2];
    pid_t pids[n];

    // Create pipes
    for (size_t i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            // no children yet, just bail
            return 1;
        }
    }

    // Fork each process in the pipeline
    for (size_t i = 0; i < n; i++) {
        char *const *argv = job->argvv[i];
        if (argv == NULL || argv[0] == NULL) {
            fprintf(stderr, "mysh: empty command in pipeline\n");
            // best effort: close pipes, wait for already-forked children
            for (size_t k = 0; k < n - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            for (size_t k = 0; k < i; k++) {
                int wstatus;
                waitpid(pids[k], &wstatus, 0);
            }
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            // Clean up: close pipes, wait for already-forked children
            for (size_t k = 0; k < n - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            for (size_t k = 0; k < i; k++) {
                int wstatus;
                waitpid(pids[k], &wstatus, 0);
            }
            return 1;
        }

        if (pid == 0) {
            // Child process i

            // Batch-mode stdin behavior: redirect to /dev/null when not interactive.
            // Pipeline stages will then override stdin with dup2() where needed.
            if (setup_stdin_for_batch(input_is_tty) < 0) {
                _exit(1);
            }

            // Connect stdin from previous pipe (not for first process)
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Connect stdout to next pipe (not for last process)
            if (i < n - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Close all pipe FDs in the child (we only need stdin/stdout now)
            for (size_t k = 0; k < n - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            // Builtin in a pipeline: run in child so it can participate
            if (is_builtin(argv[0])) {
                (void)run_builtin_child((char *const *)argv);
                _exit(0);
            }

            // External command: resolve path and execv
            char *path = resolve_program_path(argv[0]);
            if (path == NULL) {
                fprintf(stderr, "%s: command not found\n", argv[0]);
                _exit(127);
            }

            execv(path, argv);
            perror("execv");
            free(path);
            _exit(127);
        }

        // Parent: remember child PID
        pids[i] = pid;
    }

    // Parent: close all pipe FDs
    for (size_t k = 0; k < n - 1; k++) {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }

    // Wait for all children. Pipeline success is the exit code of the last one.
    int last_status = 1;
    for (size_t i = 0; i < n; i++) {
        int wstatus = 0;
        if (waitpid(pids[i], &wstatus, 0) < 0) {
            perror("waitpid");
            continue;
        }
        if (i == n - 1 && WIFEXITED(wstatus)) {
            last_status = WEXITSTATUS(wstatus);
        }
    }

    return last_status;
}

// Redirection and /dev/null behavior.
static int
setup_redirection(const char *infile, const char *outfile, bool input_is_tty)
{
    // In non-interactive mode, start by redirecting stdin to /dev/null.
    // Any explicit infile (if present) then overrides this.
    if (setup_stdin_for_batch(input_is_tty) < 0) {
        return -1;
    }

    if (infile != NULL) {
        int fd_in = open_input_file(infile);
        if (fd_in < 0) {
            return -1;
        }
        if (dup2(fd_in, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd_in);
            return -1;
        }
        close(fd_in);
    }

    if (outfile != NULL) {
        int fd_out = open_output_file(outfile);
        if (fd_out < 0) {
            return -1;
        }
        if (dup2(fd_out, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd_out);
            return -1;
        }
        close(fd_out);
    }

    return 0;
}

static int
setup_stdin_for_batch(bool input_is_tty)
{
    if (input_is_tty) {
        // Interactive mode: no special behavior.
        return 0;
    }

    // Non-tty input: redirect stdin to /dev/null.
    int fd_null = open("/dev/null", O_RDONLY);
    if (fd_null < 0) {
        perror("/dev/null");
        return -1;
    }
    if (dup2(fd_null, STDIN_FILENO) < 0) {
        perror("dup2");
        close(fd_null);
        return -1;
    }
    close(fd_null);
    return 0;
}

static int
open_input_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    return fd;
}

static int
open_output_file(const char *path)
{
    // Mode: 0640 (rw-r-----)
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    return fd;
}

// Built-in detection and dispatch.
static int
is_builtin(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, "cd")    == 0 ||
           strcmp(name, "pwd")   == 0 ||
           strcmp(name, "which") == 0 ||
           strcmp(name, "exit")  == 0 ||
           strcmp(name, "die")   == 0;
}

// Run a built-in in the parent process (for simple non-pipeline commands).
// Sets status_out to 0 on success, nonzero on failure.
// Sets action_out to EXEC_EXIT / EXEC_DIE if those builtins are invoked.
static int
run_builtin_parent(char *const argv[], int *status_out, exec_action_t *action_out)
{
    if (status_out != NULL) {
        *status_out = 1;
    }
    if (action_out != NULL) {
        *action_out = EXEC_CONTINUE;
    }

    if (argv == NULL || argv[0] == NULL) {
        if (status_out != NULL) {
            *status_out = 0;
        }
        return 0;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "cd") == 0) {
        int rc = builtin_cd(argv);
        if (status_out != NULL) {
            *status_out = rc;
        }
        return 0;
    } else if (strcmp(cmd, "pwd") == 0) {
        int rc = builtin_pwd(argv);
        if (status_out != NULL) {
            *status_out = rc;
        }
        return 0;
    } else if (strcmp(cmd, "which") == 0) {
        int rc = builtin_which(argv);
        if (status_out != NULL) {
            *status_out = rc;
        }
        return 0;
    } else if (strcmp(cmd, "exit") == 0) {
        int rc = builtin_exit(argv, action_out);
        if (status_out != NULL) {
            *status_out = rc;
        }
        return 0;
    } else if (strcmp(cmd, "die") == 0) {
        int rc = builtin_die(argv, action_out);
        if (status_out != NULL) {
            *status_out = rc;
        }
        return 0;
    }

    // Should not reach here if is_builtin() was correct.
    return -1;
}

// Run a built-in in a child process (used for pipelines).
// EXEC_EXIT / EXEC_DIE do NOT cause the shell process to exit when
// invoked in a child.
static int
run_builtin_child(char *const argv[])
{
    if (argv == NULL || argv[0] == NULL) {
        return 0;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "cd") == 0) {
        // cd in a child doesn't affect the parent shell.
        return 0;
    } else if (strcmp(cmd, "pwd") == 0) {
        return builtin_pwd(argv);
    } else if (strcmp(cmd, "which") == 0) {
        return builtin_which(argv);
    } else if (strcmp(cmd, "exit") == 0) {
        exec_action_t dummy = EXEC_CONTINUE;
        return builtin_exit(argv, &dummy);
    } else if (strcmp(cmd, "die") == 0) {
        exec_action_t dummy = EXEC_CONTINUE;
        return builtin_die(argv, &dummy);
    }

    return 0;
}


// Built-in implementations.

static int
builtin_cd(char *const argv[])
{
    // Expect exactly one argument: cd <dir>
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    if (argc != 2) {
        fprintf(stderr, "cd: expected 1 argument\n");
        return 1;
    }

    if (chdir(argv[1]) < 0) {
        perror("cd");
        return 1;
    }

    return 0;
}

static int
builtin_pwd(char *const argv[])
{
    // No extra args allowed.
    if (argv[1] != NULL) {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        perror("getcwd");
        return 1;
    }

    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

static int
builtin_which(char *const argv[])
{
    // Expect exactly one argument: which <name>
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    if (argc != 2) {
        // Wrong number of args: print nothing, fail.
        return 1;
    }

    const char *name = argv[1];

    // If it's a builtin, fail (print nothing).
    if (is_builtin(name)) {
        return 1;
    }

    char *path = resolve_program_path(name);
    if (path == NULL) {
        // Not found: print nothing, fail.
        return 1;
    }

    printf("%s\n", path);
    free(path);
    return 0;
}

static int
builtin_exit(char *const argv[], exec_action_t *action_out)
{
    // Extra arguments are ignored; spec does not prescribe behavior.
    (void)argv;

    if (action_out != NULL) {
        *action_out = EXEC_EXIT;
    }
    return 0; // success
}

static int
builtin_die(char *const argv[], exec_action_t *action_out)
{
    // Print arguments (if any) to stderr, separated by spaces, then newline.
    if (argv != NULL && argv[1] != NULL) {
        for (int i = 1; argv[i] != NULL; i++) {
            if (i > 1) {
                fputc(' ', stderr);
            }
            fputs(argv[i], stderr);
        }
        fputc('\n', stderr);
    }

    if (action_out != NULL) {
        *action_out = EXEC_DIE;
    }
    // die counts as "failure" for conditionals
    return 1;
}


// Program path resolution
// Implements the "bare names" rules from the spec:
//   - If cmd_name contains '/', treat it as a path directly.
//   - Otherwise, if it's a built-in, do not search the filesystem.
//   - Else search /usr/local/bin, /usr/bin, /bin in that order using access().
static char *
resolve_program_path(const char *cmd_name)
{
    if (cmd_name == NULL) {
        return NULL;
    }

    // If it contains '/', treat it as a direct path.
    if (strchr(cmd_name, '/') != NULL) {
        return my_strdup(cmd_name);
    }

    // Built-ins are not searched as external programs.
    if (is_builtin(cmd_name)) {
        return NULL;
    }

    const char *dirs[] = {
        "/usr/local/bin",
        "/usr/bin",
        "/bin"
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        const char *dir = dirs[i];
        size_t len = strlen(dir) + 1 + strlen(cmd_name) + 1; // dir + '/' + cmd + '\0'

        char *full = malloc(len);
        if (!full) {
            return NULL;
        }

        snprintf(full, len, "%s/%s", dir, cmd_name);

        if (access(full, X_OK) == 0) {
            return full; // found an executable
        }

        free(full);
    }

    return NULL; // not found
}
