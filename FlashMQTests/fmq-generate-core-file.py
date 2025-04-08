import time
import os


class FMQGenerateCoreFile(gdb.Command):
    """Generate a corefile with a dynamic name instead of core.PID"""

    def __init__(self):
        super().__init__("fmq-generate-core-file", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        # Are in in[MainTests::testAsserts]? If yes don't do anything (this is testing the framework on bootup)
        frame = gdb.selected_frame()
        while frame:
            # The name is a bit compiler dependent, I've seen:
            # MainTests::testAsserts
            # MainTests::testAsserts()
            frame_name = frame.name() or ''
            if frame_name.startswith("MainTests::testAsserts"):
                return False
            frame = frame.older()

        # This is something else
        try:
            runid = f".{os.environ['FMQ_RUN_ID']}."
        except KeyError:
            runid = "."
        gdb.execute(
            f"generate-core-file core{runid}{time.strftime('%Y-%d-%mT%H%M%S')}.{time.thread_time_ns()}"
        )
        issues = gdb.convenience_variable('issues') or 0
        gdb.set_convenience_variable('issues', issues + 1)


FMQGenerateCoreFile()  # this registers the CLI command
