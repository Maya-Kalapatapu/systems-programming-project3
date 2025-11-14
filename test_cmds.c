#include "mysh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// local copy of my_strdup so this file links on its own
static char *my_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) {
        memcpy(p, s, len);
    }
    return p;
}

// Generic free helper for any job_t constructed in this file
static void free_job_generic(job_t *job)
{
    if (!job) return;

    if (job->argvv) {
        // We only allocated the argvv array itself, not the inner argv strings
        free(job->argvv);
    }
    free(job->infile);
    free(job->outfile);

    memset(job, 0, sizeof(*job));
}

// Helper to build a single-command job
static void init_single_job(job_t *job, char **argv,
                            const char *infile,
                            const char *outfile)
{
    memset(job, 0, sizeof(*job));

    job->num_procs = 1;

    job->argvv = malloc(sizeof(char **));
    if (!job->argvv) {
        perror("malloc");
        exit(1);
    }
    job->argvv[0] = argv;

    job->infile  = infile  ? my_strdup(infile)  : NULL;
    job->outfile = outfile ? my_strdup(outfile) : NULL;
}

// Helper to build a 2-stage pipeline job
static void init_two_stage_pipeline(job_t *job,
                                    char **argv0,
                                    char **argv1)
{
    memset(job, 0, sizeof(*job));
    job->num_procs = 2;

    job->argvv = malloc(2 * sizeof(char **));
    if (!job->argvv) {
        perror("malloc");
        exit(1);
    }
    job->argvv[0] = argv0;
    job->argvv[1] = argv1;

    job->infile  = NULL;
    job->outfile = NULL;
}

// Tests executing a simple external command with arguments.
static void test_simple_echo(void)
{
    printf("=== test_simple_echo ===\n");
    job_t job;

    char *argv[] = { "echo", "hello", "world", NULL };
    init_single_job(&job, argv, NULL, NULL);

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/true, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", status);

    free_job_generic(&job);
}

// Tests output redirection by sending ls output into a file.
static void test_simple_ls_to_file(void)
{
    printf("=== test_simple_ls_to_file ===\n");
    job_t job;

    char *argv[] = { "ls", NULL };
    init_single_job(&job, argv, NULL, "out_ls.txt");

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/true, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n", status);
    printf("  Check contents of out_ls.txt\n\n");

    free_job_generic(&job);
}

// Tests the pwd builtin running in the parent without forking.
static void test_builtin_pwd(void)
{
    printf("=== test_builtin_pwd ===\n");
    job_t job;

    char *argv[] = { "pwd", NULL };
    init_single_job(&job, argv, NULL, NULL);

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/true, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", status);

    free_job_generic(&job);
}

// Tests the die builtin: prints message, returns failure, and requests EXEC_DIE.
static void test_builtin_die(void)
{
    printf("=== test_builtin_die ===\n");
    job_t job;

    char *argv[] = { "die", "goodbye", "cruel", "world", NULL };
    init_single_job(&job, argv, NULL, NULL);

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/true, &status);

    printf("  action=%d (expected %d=EXEC_DIE)\n", act, EXEC_DIE);
    printf("  status=%d (expected nonzero, e.g., 1)\n\n", status);

    free_job_generic(&job);
}

// Tests handling of a nonexistent external command (command-not-found case).
static void test_bad_command(void)
{
    printf("=== test_bad_command ===\n");
    job_t job;

    char *argv[] = { "this-command-better-not-exist-12345", NULL };
    init_single_job(&job, argv, NULL, NULL);

    int status = -1;
    exec_action_t act = execute_job(&job, true, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected nonzero)\n\n", status);

    free_job_generic(&job);
}

// Tests batch-mode stdin behavior: non-tty input redirects stdin to /dev/null.
static void test_batch_stdin_null(void)
{
    printf("=== test_batch_stdin_null ===\n");
    job_t job;

    char *argv[] = { "cat", NULL };
    init_single_job(&job, argv, NULL, NULL);

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/false, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (cat should not hang; likely 0 or nonzero but must terminate)\n\n", status);

    free_job_generic(&job);
}

// Tests a simple two-stage pipeline: echo hello | wc -c.
static void test_pipeline_echo_wc(void)
{
    printf("=== test_pipeline_echo_wc ===\n");
    job_t job;

    char *argv0[] = { "echo", "hello", NULL };
    char *argv1[] = { "wc", "-c", NULL };

    init_two_stage_pipeline(&job, argv0, argv1);

    int status = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/true, &status);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", status);

    free_job_generic(&job);
}


int main(void)
{
    test_simple_echo();
    test_simple_ls_to_file();
    test_builtin_pwd();
    test_builtin_die();
    test_bad_command();
    test_batch_stdin_null();
    test_pipeline_echo_wc();

    return 0;
}
