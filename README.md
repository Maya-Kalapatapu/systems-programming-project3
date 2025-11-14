# My Shell (mysh)

## Build and Run (Quick Start)
- Build shell:  
  `make`
- Build & run tests:  
  `make test_cmds`  
  `./test_cmds`
- Run interactively:  
  `./mysh`
- Run batch mode:  
  `./mysh script.txt`  
  or  
  `cat script.txt | ./mysh`

## Execution Layer Summary
- External commands executed via `fork` + `execv`, searching `/usr/local/bin`, `/usr/bin`, `/bin` unless the name contains `/`.
- Built-ins supported: `cd`, `pwd`, `which`, `exit`, `die`.  
  • In single-command jobs, built-ins run in the parent.  
  • In pipelines, built-ins run in child processes.
- Redirection with `< infile` and `> outfile` using `open` + `dup2`.  
  Output files created with mode `0640`.
- Pipelines (`cmd1 | cmd2 | ...`) use `N-1` pipes and fork all stages.  
  Pipeline exit status is the exit code of the final command.
- Batch mode (`input_is_tty == false`): stdin automatically becomes `/dev/null` unless `< infile` is used.

## Files Included
- `mysh_cmds.c`: Implements command execution, pipelines, redirection, built-ins, and path resolution.
- `mysh.h`: Defines `job_t`, `exec_action_t`, and function interfaces.
- `test_cmds.c`: Standalone tests for the execution layer (no parser needed).

## Running Tests
- After `make test_cmds`, run:  
  `./test_cmds`
- Redirected output (from the ls test) appears in:  
  `out_ls.txt`

