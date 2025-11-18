#include "mysh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/***********************
 * Utility helpers
 ***********************/
static char *dupstr(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p) { perror("malloc"); exit(1); }
    memcpy(p, s, len);
    return p;
}

static void free_job_allocated_by_us(job_t *job) {
    if (!job) return;

    // We only free the outer allocations we made in this file.
    if (job->argvv)  free(job->argvv);
    if (job->infile) free(job->infile);
    if (job->outfile) free(job->outfile);

    memset(job, 0, sizeof(*job));
}

static void init_single(job_t *job, char **argv, const char *in, const char *out) {
    memset(job, 0, sizeof(*job));

    job->num_procs = 1;
    job->argvv = malloc(sizeof(char**));
    if (!job->argvv) { perror("malloc"); exit(1); }

    job->argvv[0] = argv;
    job->infile  = in  ? dupstr(in)  : NULL;
    job->outfile = out ? dupstr(out) : NULL;
}

static void init_pipeline_two(job_t *job, char **a, char **b) {
    memset(job, 0, sizeof(*job));

    job->num_procs = 2;
    job->argvv = malloc(2 * sizeof(char**));
    if (!job->argvv) { perror("malloc"); exit(1); }

    job->argvv[0] = a;
    job->argvv[1] = b;
}

/***********************
 * Parse tests (CORE)
 ***********************/
static void test_parse_simple() {
    printf("=== test_parse_simple ===\n");

    job_t job = {0};
    char line[] = "echo hi";
    int r = parse_line(line, &job);

    printf("  parse returned %d (expected 1)\n", r);
    printf("  num_procs=%zu (expected 1)\n", job.num_procs);
    if (!job.argvv || !job.argvv[0] || !job.argvv[0][0]) {
        printf("  FAIL: argvv[0][0] missing\n");
    } else if (strcmp(job.argvv[0][0], "echo") != 0) {
        printf("  FAIL: argvv[0][0] expected 'echo', got '%s'\n", job.argvv[0][0]);
    }

    free_job(&job);
    printf("\n");
}

static void test_parse_pipeline() {
    printf("=== test_parse_pipeline ===\n");

    job_t job = {0};
    char line[] = "echo hi | wc";
    int r = parse_line(line, &job);

    printf("  parse returned %d (expected 1)\n", r);
    printf("  num_procs=%zu (expected 2)\n", job.num_procs);

    if (!job.argvv || job.num_procs != 2) {
        printf("  FAIL: expected 2 stages\n");
    } else {
        if (!job.argvv[0] || !job.argvv[0][0] ||
            strcmp(job.argvv[0][0], "echo") != 0) {
            printf("  FAIL: stage 0 expected 'echo'\n");
        }
        if (!job.argvv[1] || !job.argvv[1][0] ||
            strcmp(job.argvv[1][0], "wc") != 0) {
            printf("  FAIL: stage 1 expected 'wc'\n");
        }
    }

    free_job(&job);
    printf("\n");
}

static void test_parse_redirs() {
    printf("=== test_parse_redirs ===\n");

    job_t job = {0};
    char line[] = "cat < infile.txt > outfile.txt";
    int r = parse_line(line, &job);

    printf("  parse returned %d (expected 1)\n", r);
    printf("  infile=%s (expected 'infile.txt')\n",
           job.infile ? job.infile : "(null)");
    printf("  outfile=%s (expected 'outfile.txt')\n",
           job.outfile ? job.outfile : "(null)");

    free_job(&job);
    printf("\n");
}

static void test_parse_conditional_errors() {
    printf("=== test_parse_conditional_errors ===\n");

    job_t job;
    int r;

    // leading 'and' is syntactically ok for the parser; enforced by core loop
    memset(&job, 0, sizeof(job));
    char line1[] = "and echo hi";
    r = parse_line(line1, &job);
    printf("  leading 'and' parse returned %d (expected 1; runtime enforces restriction)\n", r);
    free_job(&job);

    // conditional after pipe is a true parse error
    memset(&job, 0, sizeof(job));
    char line2[] = "echo hi | and echo no";
    r = parse_line(line2, &job);
    printf("  pipe then 'and' returned %d (expected -1)\n", r);
    free_job(&job);

    // bare 'or' is also a parse error
    memset(&job, 0, sizeof(job));
    char line3[] = "or";
    r = parse_line(line3, &job);
    printf("  bare 'or' returned %d (expected -1)\n", r);
    free_job(&job);

    printf("\n");
}

static void test_parse_comment_only(void) {
    printf("=== test_parse_comment_only ===\n");
    job_t job = {0};
    char line[] = "# just a comment";
    int r = parse_line(line, &job);
    printf("  parse returned %d (expected 0)\n", r);
    printf("  num_procs=%zu (expected 0)\n\n", job.num_procs);
    // no need to free_job; parser should not have allocated anything
}

static void test_parse_trailing_comment(void) {
    printf("=== test_parse_trailing_comment ===\n");
    job_t job = {0};
    char line[] = "echo hi # trailing comment";
    int r = parse_line(line, &job);
    printf("  parse returned %d (expected 1)\n", r);
    if (job.num_procs != 1 || strcmp(job.argvv[0][0], "echo") != 0) {
        printf("  FAIL: expected single echo command\n");
    }
    free_job(&job);
    printf("\n");
}

// both directions in opposite order
static void test_parse_redirs_order_flipped(void) {
    printf("=== test_parse_redirs_order_flipped ===\n");
    job_t job = {0};
    char line[] = "cat > out2.txt < in2.txt";
    int r = parse_line(line, &job);
    printf("  parse returned %d (expected 1)\n", r);
    printf("  infile=%s (expected 'in2.txt')\n", job.infile);
    printf("  outfile=%s (expected 'out2.txt')\n", job.outfile);
    free_job(&job);
    printf("\n");
}

// multiple input redirections error
static void test_parse_multiple_input_redirs(void) {
    printf("=== test_parse_multiple_input_redirs ===\n");
    job_t job = {0};
    char line[] = "cat < a < b";
    int r = parse_line(line, &job);
    printf("  parse returned %d (expected -1)\n", r);
    free_job(&job);
    printf("\n");
}

// missing filename after redirection
static void test_parse_redir_missing_filename(void) {
    printf("=== test_parse_redir_missing_filename ===\n");
    job_t job = {0};
    char line[] = "cat <";
    int r = parse_line(line, &job);
    printf("  'cat <' returned %d (expected -1)\n", r);
    free_job(&job);
    printf("\n");
}

static void test_parse_conditional_flags(void) {
    printf("=== test_parse_conditional_flags ===\n");
    job_t job = {0};
    char line1[] = "and echo hi";
    int r1 = parse_line(line1, &job);
    printf("  'and echo hi' parse=%d, cond=%d (expected 1, COND_AND=%d)\n",
           r1, job.cond, COND_AND);
    free_job(&job);

    job_t job2 = {0};
    char line2[] = "or echo hi";
    int r2 = parse_line(line2, &job2);
    printf("  'or echo hi' parse=%d, cond=%d (expected 1, COND_OR=%d)\n",
           r2, job2.cond, COND_OR);
    free_job(&job2);

    printf("\n");
}



/***********************
 * Execution tests (CMDS)
 ***********************/
static void test_exec_echo() {
    printf("=== test_exec_echo ===\n");

    job_t job;
    char *av[] = { "echo", "hello", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", st);

    free_job_allocated_by_us(&job);
}

static void test_exec_ls_outfile() {
    printf("=== test_exec_ls_outfile ===\n");

    job_t job;
    char *av[] = { "ls", NULL };
    init_single(&job, av, NULL, "test_ls.txt");

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n", st);
    printf("  Check test_ls.txt exists and has content.\n\n");

    free_job_allocated_by_us(&job);
}

static void test_exec_pwd_builtin() {
    printf("=== test_exec_pwd_builtin ===\n");

    job_t job;
    char *av[] = { "pwd", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", st);

    free_job_allocated_by_us(&job);
}

static void test_exec_die() {
    printf("=== test_exec_die ===\n");

    job_t job;
    char *av[] = { "die", "boom", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_DIE)\n", act, EXEC_DIE);
    printf("  status=%d (expected nonzero)\n\n", st);

    free_job_allocated_by_us(&job);
}

static void test_exec_missing_cmd() {
    printf("=== test_exec_missing_cmd ===\n");

    job_t job;
    char *av[] = { "no_such_command_9999", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected nonzero)\n\n", st);

    free_job_allocated_by_us(&job);
}

static void test_exec_pipeline() {
    printf("=== test_exec_pipeline ===\n");

    job_t job;
    char *a[] = { "echo", "hello", NULL };
    char *b[] = { "wc", "-c", NULL };
    init_pipeline_two(&job, a, b);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n\n", st);

    free_job_allocated_by_us(&job);
}

static void test_exec_exit(void) {
    printf("=== test_exec_exit ===\n");
    job_t job;
    char *av[] = { "exit", NULL };
    init_single(&job, av, NULL, NULL);
    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);
    printf("  action=%d (expected %d=EXEC_EXIT)\n", act, EXEC_EXIT);
    printf("  status=%d (expected 0)\n\n", st);
    free_job_allocated_by_us(&job);
}

static void test_batch_stdin_null(void) {
    printf("=== test_batch_stdin_null ===\n");
    job_t job;
    char *av[] = { "cat", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, /*input_is_tty=*/false, &st);
    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (cat should not hang; any code is fine as long as it returns)\n\n", st);

    free_job_allocated_by_us(&job);
}

// --------------------------------------------------
// which builtin tests
// --------------------------------------------------
static void test_exec_which_external(void) {
    printf("=== test_exec_which_external ===\n");

    job_t job;
    char *av[] = { "which", "ls", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected 0)\n", st);
    printf("  (Output should be a path to ls)\n\n");

    free_job_allocated_by_us(&job);
}

static void test_exec_which_builtin(void) {
    printf("=== test_exec_which_builtin ===\n");

    job_t job;
    char *av[] = { "which", "cd", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected nonzero; builtin should not be found as external)\n", st);
    printf("  (Expected: no output)\n\n");

    free_job_allocated_by_us(&job);
}

static void test_exec_which_missing(void) {
    printf("=== test_exec_which_missing ===\n");

    job_t job;
    char *av[] = { "which", "definitely_does_not_exist_12345", NULL };
    init_single(&job, av, NULL, NULL);

    int st = -1;
    exec_action_t act = execute_job(&job, true, &st);

    printf("  action=%d (expected %d=EXEC_CONTINUE)\n", act, EXEC_CONTINUE);
    printf("  status=%d (expected nonzero)\n", st);
    printf("  (Expected: no output)\n\n");

    free_job_allocated_by_us(&job);
}


/***********************
 * Main test runner
 ***********************/
int main(void) {
    printf("======== PARSE TESTS ========\n");
    test_parse_simple();
    test_parse_pipeline();
    test_parse_redirs();
    test_parse_redirs_order_flipped();
    test_parse_multiple_input_redirs();
    test_parse_redir_missing_filename();
    test_parse_comment_only();
    test_parse_trailing_comment();
    test_parse_conditional_errors();
    test_parse_conditional_flags();

    printf("======== EXEC TESTS ========\n");
    test_exec_echo();
    test_exec_ls_outfile();
    test_exec_pwd_builtin();
    test_exec_die();
    test_exec_missing_cmd();
    test_exec_pipeline();
    test_exec_exit();
    test_batch_stdin_null();
    test_exec_which_external();
    test_exec_which_builtin();
    test_exec_which_missing();
    

    return 0;
}
