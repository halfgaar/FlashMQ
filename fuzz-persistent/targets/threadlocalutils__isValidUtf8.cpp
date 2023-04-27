#include <unistd.h>

#ifndef __SSE4_2__
#error "This file is supposed to test SIMD, please fix your setup to support it."
#endif

#include "../../threadlocalutils.h"
thread_local SimdUtils simdUtils;

__AFL_FUZZ_INIT();

int main()
{
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF; // must be after __AFL_INIT
                                                  // and before __AFL_LOOP!
    char *signed_buff = reinterpret_cast<char *>(buf);

    while (__AFL_LOOP(10000))
    {

        int len = __AFL_FUZZ_TESTCASE_LEN; // don't use the macro directly in a
                                           // call!
        std::string random_string(signed_buff, len);
        simdUtils.isValidUtf8(random_string, false);
        simdUtils.isValidUtf8(random_string, true);
    }

    return 0;
}
