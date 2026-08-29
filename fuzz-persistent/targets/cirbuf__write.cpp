#include <unistd.h>
#include "../../cirbuf.h"

__AFL_FUZZ_INIT();

int main()
{
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    // call to __AFL_FUZZ_TESTCASE_BUF must be after __AFL_INIT and before __AFL_LOOP
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000))
    {
        // Don't use the __AFL_FUZZ_TESTCASE_LEN macro direct in a call
        int len = __AFL_FUZZ_TESTCASE_LEN;

        CirBuf cirbuf(1024);
        cirbuf.write(buf, len);
    }

    return 0;
}
