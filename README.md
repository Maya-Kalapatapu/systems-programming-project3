# My Shell (mysh)

## Build, Run, and Test
- Build shell:  
  `make`
- Run interactively:  
  `./mysh`
- Run batch mode with a script:  
  `./mysh script.txt`  
  or  
  `cat script.txt | ./mysh`
- Build & run tests:  
  `make test`  
  `./test`  
  The `ls` execution test creates `test_ls.txt` to verify output redirection.

## Batch Mode (script.txt Included)
A sample script (`script.txt`) is included to demonstrate batch execution, pipelines, conditionals, and redirection.  
When run in batch mode, its redirected output is written to `sample_output.txt`.

## Command Format
- One job per line.  
- Tokens are whitespace-separated; `<`, `>`, and `|` are always separate tokens.  
- `#` begins a comment until end of line.
- Redirection:
  - `< infile` sets stdin.
  - `> outfile` sets stdout (created with permissions `0640`).
  - Multiple redirects of the same type are an error.
- Pipelines: `cmd1 | cmd2 | ... | cmdN`
- Conditionals:
  - `and` / `or` may appear **only at the start** of a job.
  - `and` runs only if the previous job succeeded (status 0).
  - `or` runs only if the previous job failed (status != 0).
  - Conditionals cannot appear on the first job.

## Parsing Layer Summary
- Input is read using `read()` only.  
- Each newline-terminated line is parsed and executed immediately.  
- Bytes remaining at EOF (without newline) form one final job.

## Execution Layer Summary
- External commands: `fork` + `execv`, searching `/usr/local/bin`, `/usr/bin`, `/bin` unless the command contains `/`.
- Built-ins: `cd`, `pwd`, `which`, `exit`, `die`.  
  - Single commands: built-ins run in the parent.  
  - Pipelines: built-ins run in children.
- Redirection handled using `open` and `dup2`.  
- Pipelines use `N-1` pipes; the exit status of the final command is returned.
- Batch mode: when stdin is not a TTY, commands default to reading from `/dev/null` unless overridden with `< infile`.
- Shell exit codes:
  - Normal termination = success.  
  - `die` = failure.  
  - Script file open failure = failure.

## Testing
The project includes a test runner (`test`) verifying both parsing and execution.

Run the full suite:
- `make test`
- `./test`

The tests cover:
- **Parsing**
  - Simple commands  
  - Pipelines  
  - Redirection handling  
  - Syntax errors (missing filenames, repeated redirects, invalid conditionals, comment-only lines, trailing comments)  
  - Conditional parsing (`and` / `or`)
- **Execution**
  - External commands (`echo`, `ls`, `wc`)  
  - Built-ins (`cd`, `pwd`, `exit`, `die`, `which`)  
  - Pipelines (`echo hello | wc -c`)  
  - Output redirection (`ls > test_ls.txt`)  
  - Unknown commands  
  - Batch-mode stdin behavior  
  - Built-ins in single-command and pipeline contexts

### Test Artifacts
- `test_ls.txt` – produced by the `ls` redirection test.  
- `sample_output.txt` – produced when running `./mysh script.txt`.  
Both are removed by `make clean`.

## Files Included
- `mysh_core.c` — input loop, parsing, conditionals, job dispatch.  
- `mysh_cmds.c` — execution engine (process creation, redirection, pipelines, built-ins).  
- `mysh.h` — shared types and function interfaces.  
- `test.c` — test suite for parsing and execution.  
- `script.txt` — sample batch script.  
