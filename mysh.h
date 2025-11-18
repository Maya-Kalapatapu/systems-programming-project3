#ifndef MYSH_H
#define MYSH_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_TOKENS        1024
#define MAX_COMMANDS      64
#define MAX_ARGS          64
#define INPUT_BUFFER_SIZE 4096
#define PROMPT "mysh> "

/*
 * Condition for running this command based on the previous command's result.
 * COND_AND:  run only if previous succeeded  (status == 0)
 * COND_OR:   run only if previous failed     (status != 0)
 */
typedef enum {
    COND_NONE = 0,
    COND_AND,
    COND_OR
} condition_t;

/*
 * Representation of a single "job": either
 *   - a simple command, or
 *   - a pipeline of multiple commands.
 *
 * For pipelines:
 *   - argvv[i] is the argv array (NULL-terminated) for the i-th process.
 *   - num_procs is the number of processes in the pipeline.
 *
 * Per spec, callers may assume that if num_procs > 1 (a pipeline), then
 * infile and outfile are NULL (no redirection with pipelines).
 */
typedef struct {
    char ***argvv;     /* argvv[i] is NULL-terminated argv for process i */
    size_t num_procs;  /* number of processes in pipeline (>= 1) */

    char *infile;      /* input redirection filename, or NULL */
    char *outfile;     /* output redirection filename, or NULL */

    condition_t cond;  /* leading 'and' / 'or' token for this command */
} job_t;

/*
 * Result of executing a job, from the shell's perspective.
 *
 * - EXEC_CONTINUE: keep reading and executing more commands
 * - EXEC_EXIT:     built-in "exit" was executed; shell should exit SUCCESS
 * - EXEC_DIE:      built-in "die" was executed; shell should exit FAILURE
 */
typedef enum {
    EXEC_CONTINUE = 0,
    EXEC_EXIT,
    EXEC_DIE
} exec_action_t;

/*
 * Execute a parsed job.
 *
 * Parameters:
 *   job          - parsed job description (argvs, pipes, redirection, etc.)
 *   input_is_tty - true if commands are being read interactively from
 *                  a terminal; false when running a script or when mysh's
 *                  standard input is not a terminal. When false, child
 *                  processes will have stdin redirected to /dev/null unless
 *                  overridden by infile.
 *   cmd_status   - if non-NULL, will be set to the exit status of the job:
 *                    0     => success
 *                    != 0  => failure
 *
 * Returns:
 *   EXEC_CONTINUE, EXEC_EXIT, or EXEC_DIE to tell the core loop what to do.
 *
 * Implemented in mysh_cmds.c.
 */
exec_action_t execute_job(const job_t *job,
                          bool input_is_tty,
                          int *cmd_status);

/*
 * Free all dynamic memory associated with a job.
 *
 * Assumes that any strings / arrays inside job were allocated by the parser.
 * Safe to call even if parts of job are NULL.
 *
 * Implemented in mysh_core.c (with the parser), but shared in the header
 * because both mysh_core.c and mysh_cmds.c may need to call it.
 */
void free_job(job_t *job);

/*
 * Parse a single input line into a job_t.
 *
 * Returns:
 *   1  on success (job filled in),
 *   0  for an empty / comment-only line (job left empty),
 *  -1  on syntax or allocation error (job reset to empty).
 */
int parse_line(char *line, job_t *job);

#endif
