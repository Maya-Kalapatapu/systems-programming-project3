# My Shell (mysh)

## Build, Run, and Test
- Build shell:  
  `make`
- Run interactively:  
  `./mysh`
- Run batch mode:  
  `./mysh script.txt`  
  or  
  `cat script.txt | ./mysh`
- Build & run tests:  
  `make test`  
  `./test`  
  Test output from the `ls` redirection test appears in `test_ls.txt`.

## Command Format
- One job per line.  
- Tokens are whitespace-separated; `<`, `>`, and `|` are always separate tokens.  
- `#` begins a comment until end of line.
- Redirection:
  - `< infile` sets stdin.
  - `> outfile` sets stdout (mode `0640`).
  - Multiple redirects of the same type are an error.
- Pipelines:  
  `cmd1 | cmd2 | ... | cmdN`
- Conditionals:
  - `and` or `or` may appear **only at the start** of a job.
  - `and` runs only if the previous job's exit status was 0.
  - `or` runs only if the previous job's exit status was non-zero.
  - Conditionals cannot appear on the first job.
- A final command without a trailing newline is still parsed and executed.

## Parsing Layer Summary
- Input is read using `read()` only.  
- Each newline-terminated line is parsed and executed immediately.  
- Any remaining bytes at EOF (no final newline) are processed as one last command.

## Execution Layer Summary
- External commands: `fork` + `execv`, searching `/usr/local/bin`, `/usr/bin`, `/bin` unless the name contains `/`.
- Built-ins: `cd`, `pwd`, `which`, `exit`, `die`.  
  - Single-command jobs: built-ins run in the parent.  
  - Pipelines: built-ins run in children.
- Redirection: performed with `open` and `dup2`.  
- Pipelines: create `N-1` pipes, fork each stage, return the exit status of the final command.
- Batch mode (non-TTY): stdin defaults to `/dev/null` unless `< infile` overrides it.
- Shell exit codes:  
  - Normal termination → success.  
  - `die` → failure.  
  - Failure to open script file → failure.

## Testing
The project includes a standalone test runner (`test`) that verifies both parsing and execution behavior.

### Running the Test Suite
```bash
make test
./test
```

### What the Tests Cover

**Parsing Tests**
- Simple commands (`echo hi`)
- Pipelines (`echo hi | wc`)
- Redirections (`< infile`, `> outfile`, in any order)
- Syntax errors:
  - Missing redirection filenames  
  - Multiple redirects of same type  
  - Conditionals in invalid positions  
  - Comment-only lines  
  - Trailing comments  
- Conditional flag parsing (`and`, `or`)

**Execution Tests**
- External commands (`echo`, `ls`, `wc`)
- Built-ins (`cd`, `pwd`, `exit`, `die`, `which`)
- Pipelines (`echo hello | wc -c`)
- Output redirection (`ls > test_ls.txt`)
- Unknown command errors  
- Batch mode stdin redirection  
- Built-in behavior in single-command and pipeline contexts  

### Test Artifacts
- `test_ls.txt` – created by the `ls` redirection test; removed by `make clean`.

## Files Included
- `mysh_core.c` — Input loop, parsing, conditionals, job dispatch.  
- `mysh_cmds.c` — Execution layer, pipelines, redirection, built-ins, PATH search.  
- `mysh.h` — Shared types and interfaces (`job_t`, `exec_action_t`, etc.).  
- `test.c` — Test suite for parsing and execution.
