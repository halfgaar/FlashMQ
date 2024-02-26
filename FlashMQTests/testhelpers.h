#ifndef TESTHELPERS_H
#define TESTHELPERS_H

#include <sstream>

#define RED "\033[01;31m"
#define GREEN "\033[01;32m"
#define CYAN "\033[01;36m"
#define COLOR_END "\033[00m"

extern int assert_count;
extern int assert_fail_count;
extern bool asserts_print;

bool fmq_assert(bool b, const char *failmsg, const char *actual, const char *expected, const char *file, int line);
void fmq_fail(const char *failmsg, const char *file, int line);


#define FMQ_COMPARE(actual, expected) \
do { \
if (!fmq_compare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return; \
} while(false)

#define FMQ_VERIFY(val) \
do { \
if (!fmq_assert(static_cast<bool>(val), "assertion failed", #val, "true", __FILE__, __LINE__))\
        return; \
} while (false)

#define FMQ_VERIFY2(val, failmsg) \
do { \
if (!fmq_assert(static_cast<bool>(val), failmsg, #val, "true", __FILE__, __LINE__))\
        return; \
} while (false)

#define FMQ_FAIL(msg) fmq_fail(msg, __FILE__, __LINE__)

// Compatability for porting the tests away from Qt.
#define QCOMPARE(actual, expected) FMQ_COMPARE(actual, expected)
#define QVERIFY(val) FMQ_VERIFY(val)
#define QVERIFY2(val, failmsg) FMQ_VERIFY2(val, failmsg)
#define QFAIL(msg) FMQ_FAIL(msg)

inline bool fmq_compare(const std::string &s1, const std::string &s2, const char *actual, const char *expected, const char *file, int line)
{
    std::ostringstream oss;
    oss << s1 << " != " << s2;
    return fmq_assert(s1 == s2, oss.str().c_str(), actual, expected, file, line);
}

template <typename T1, typename T2>
inline bool fmq_compare(const T1 &t1, const T2 &t2, const char *actual, const char *expected, const char *file, int line)
{
    return fmq_assert(t1 == t2, "Values aren't the same", actual, expected, file, line);
}

inline bool fmq_compare(const char *c1, const char *c2, const char *actual, const char *expected, const char *file, int line)
{
    std::string s1(c1);
    std::string s2(c2);

    std::ostringstream oss;
    oss << s1 << " != " << s2;

    return fmq_assert(s1 == s2, oss.str().c_str(), actual, expected, file, line);
}

template <typename T1, typename T2>
inline bool myCastCompare(const T1 &t1, const T2 &t2, const char *actual, const char *expected,
                          const char *file, int line)
{
    T1 t2_ = static_cast<T1>(t2);
    return fmq_compare(t1, t2_, actual, expected, file, line);
}

#define MYCASTCOMPARE(actual, expected) \
do {\
        if (!myCastCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return;\
} while (false)


#endif // TESTHELPERS_H
