# Less verbose gdb please
set print thread-events off
set print inferior-events off

# fmq-generate-core-file will increase $issues when it dumps
set $issues = 0

catch signal SIGVTALRM
commands
  fmq-generate-core-file
  continue
end

run

exit $issues
