# Fuzzing with persistent mode

See the [AFL++ documentation](https://github.com/AFLplusplus/AFLplusplus/blob/stable/instrumentation/README.persistent_mode.md) for generic help about fuzzing with persistent mode.

TL;DR: it's _faaaaaast_.

## Fuzzing everything

Simply run `./fuzz-helper.sh`. Findings will be written to the folder called `output`.

## Setting up new test

To add a new test:

* Decide on a testname, the standard format is: `<sourcefilename>__<test name (fe. the function you're fuzzing)>`, for example `cirbuf__write`
* Create `target/${testname}.cpp`
* Run `./fuzz-helper.sh build_and_run ${testname}`
