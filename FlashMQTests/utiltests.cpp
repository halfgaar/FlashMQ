#include <random>

#include "maintests.h"
#include "testhelpers.h"

#include "utils.h"
#include "exceptions.h"


void MainTests::testStringValuesParsing()
{
    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("one two three");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "one");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("\"one\" \"two\" three");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "one");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("\"one\" two \"three\"");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "one");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("\"one\" \"two\" \"three\"");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "one");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("\"o'ne\" \"two'\" \"three\"");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "o'ne");
        FMQ_COMPARE(result.at(1), "two'");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("'one' 'two' 'three'");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "one");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("'o\"ne' 'two' 'three'");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "o\"ne");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("'o\"ne' 'two' 'three' ");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), "o\"ne");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>("");
        MYCASTCOMPARE(result.size(), 0);
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(one"")delim");
        MYCASTCOMPARE(result.size(), 1);
        FMQ_COMPARE(result.at(0), R"delim(one)delim");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(one"two")delim");
        MYCASTCOMPARE(result.size(), 1);
        FMQ_COMPARE(result.at(0), R"delim(onetwo)delim");
    }
}

void MainTests::testStringValuesParsingEscaping()
{
    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(on\"e two three)delim");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), R"delim(on"e)delim");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(t\\wo three)delim");
        MYCASTCOMPARE(result.size(), 2);
        FMQ_COMPARE(result.at(0), R"delim(t\wo)delim");
        FMQ_COMPARE(result.at(1), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(on\"e 'two' three)delim");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), R"delim(on"e)delim");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), "three");
    }

    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<std::runtime_error>(R"delim(on\"e 'two'   'thr"ee')delim");
        MYCASTCOMPARE(result.size(), 3);
        FMQ_COMPARE(result.at(0), R"delim(on"e)delim");
        FMQ_COMPARE(result.at(1), "two");
        FMQ_COMPARE(result.at(2), R"delim(thr"ee)delim");
    }

}

void MainTests::testStringValuesFuzz()
{
    std::vector<std::string> words;

    std::minstd_rand rnd;
    rnd.seed(16893578);

    for (uint32_t i = 0; i < 10000; i++)
    {
        uint32_t v = rnd() % 16;

        for (uint32_t j = 0; j < v; j++)
        {
            uint32_t w = rnd() % 10;

            std::string word;
            for (uint32_t k = 0; k < w; k++)
            {
                char random_char = rnd() % 256;
                word.push_back(random_char);
            }

            words.push_back(word);
        }
    }

    FMQ_VERIFY(words.size() > 10000);

    for (char q : {'"', '\''})
    {
        std::string longline;

        for(const std::string &w : words)
        {
            std::string w2;

            for(char c : w)
            {
                if (c == '"' || c == '\'' || c == '\\')
                    w2.push_back('\\');
                w2.push_back(c);
            }

            longline.push_back(q);
            longline.append(w2);
            longline.push_back(q);
            longline.push_back(' ');
        }

        std::vector<std::string> parsed = parseValuesWithOptionalQuoting<std::runtime_error>(longline);

        FMQ_COMPARE(parsed, words);
    }
}

void MainTests::testStringValuesInvalid()
{
    try
    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<ConfigFileException>("on\\\"e 'two'   'thr\"ee");
        FMQ_VERIFY(false);
    }
    catch (ConfigFileException &ex)
    {
        std::string s(ex.what());
        FMQ_VERIFY(s.find("Unterminated quote") != std::string::npos);
    }
    catch (std::exception &ex)
    {
        FMQ_VERIFY(false);
    }


    try
    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<ConfigFileException>("This is an \\invalid escape.");
        FMQ_VERIFY(false);
    }
    catch (ConfigFileException &ex)
    {
        std::string s(ex.what());
        FMQ_VERIFY(s.find("Invalid escape") != std::string::npos);
    }
    catch (std::exception &ex)
    {
        FMQ_VERIFY(false);
    }


    try
    {
        std::vector<std::string> result = parseValuesWithOptionalQuoting<ConfigFileException>("Escaping space is not support \\  bla");
        FMQ_VERIFY(false);
    }
    catch (ConfigFileException &ex)
    {
        std::string s(ex.what());
        FMQ_VERIFY(s.find("Invalid escape") != std::string::npos);
    }
    catch (std::exception &ex)
    {
        FMQ_VERIFY(false);
    }
}








