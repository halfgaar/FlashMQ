# NB: the gdb object is given to us by gdb, we don't need to import it

# Making gdb a bit quieter
gdb.execute("set print thread-events off")
gdb.execute("set print inferior-events off")

# since our class runs in a separate thread and has different variables we'll
# need to store $issues inside GDB itself
gdb.execute("set $issues = 0")

class ShowFrames(gdb.Breakpoint):
    def stop(self):
        # Are in in[MainTests::testAsserts]? If yes don't do anything (this is testing the framework on bootup)
        frame = gdb.selected_frame()
        while frame:
            # The name is a bit compiler dependent, I've seen:
            # MainTests::testAsserts
            # MainTests::testAsserts()
            if frame.name().startswith("MainTests::testAsserts"):
                return False
            frame = frame.older()

        # This is a real assert triggering
        gdb.execute("set $issues += 1")

        frame = gdb.selected_frame()

        indentation = ""
        while frame:
            print(f"{indentation}- [{frame.name()}]")
            indentation += "  "

            try:
                block = frame.block()
            except:
                #  doc: If the frame does not have a block – for example, if there is no debugging information for the code in question – then this will throw an exception.
                pass
            else:
                for variable in block:
                    print(f"{indentation}{variable.print_name} = ", end='')
                    if variable.print_name in ['maintests']:
                        print("(skipped)")
                    else:
                        try:
                            print(f"{variable.value(frame)} ({variable.type})")
                        except Exception as e:
                            print(f"({e})")
            if frame.name().startswith('MainTests::'):
                # if we go further up we'll end up in test runner's code, not too interesting
                break
            frame = frame.older()

        return False

ShowFrames("trigger_gdb_if_attached")
gdb.execute("run")
gdb.execute("quit $issues")
