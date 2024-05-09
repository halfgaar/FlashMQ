#include <random>

#include "maintests.h"
#include "testhelpers.h"

#include "utils.h"
#include "exceptions.h"
#include "conffiletemp.h"

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

/**
 * @brief MainTests::testPreviouslyValidConfigFile tests a config file that worked before the new parser. It uses as many of
 * the allowed syntax as possible.
 */
void MainTests::testPreviouslyValidConfigFile()
{
    // Base64 is required because my editor starts interferring with the content, like leading tabs on lines.
    const std::string b64(
        "I3RocmVhZF9jb3VudCA2NAoKI2xvZ19maWxlIC90bXAvYm9lMy5sb2cnCgojIComXigqXiooJl4K"
        "IykoKikoKgogICAgICAjICkoKikoCgkjIHRoaXMgaXMgYSB0YWIKCiMgVGhpcyBsaW5lIGNvbnRh"
        "aW5zIGEgdGFiCglhbGxvd19hbm9ueW1vdXMgdHJ1ZQoKI292ZXJsb2FkX21vZGUgY2xvc2VfbmV3"
        "X2NsaWVudHMKCnBsdWdpbl9vcHRfb25lICAgICAgYWJjY2M5YyNjZGVmQUVCRjRfLS86KysrCgog"
        "ICAgcGx1Z2luX3RpbWVyX3BlcmlvZCAgICAgNDAwMAoKbG9nX2xldmVsIG5vdGljZSAgIAoKd2Vi"
        "c29ja2V0X3NldF9yZWFsX2lwX2Zyb20gICAgICAgIDJhOjMzOjozMzowMC82NAoKbGlzdGVuIHsK"
        "ICBwcm90b2NvbCBtcXR0CiAgcG9ydCAxODgzCiAgdGNwX25vZGVsYXkgZmFsc2UKCn0KCmxpc3Rl"
        "biB7CiAgcHJvdG9jb2wgbXF0dAogIHBvcnQgODA3MAoKfQoKYnJpZGdlIHsKICBsb2NhbF91c2Vy"
        "bmFtZSBhYmNkZSNmQUVCRjRfLS86KwogIGFkZHJlc3MgZGVtby5mbGFzaG1xLm9yZwogIGNsaWVu"
        "dGlkX3ByZWZpeCBCckQKICBwdWJsaXNoIGZpdmUgMgogIHN1YnNjcmliZSBhc2RmYXNkZmVlL3dl"
        "ci8rIDIKICBzdWJzY3JpYmUgX19fNDQvd2VyLyMgMQogIGNsaWVudGlkX3ByZWZpeCB6SDUzXy06"
        "Ol8KICB0Y3Bfbm9kZWxheSB0cnVlCn0K");

    const std::vector<char> b64_bytes = base64Decode(b64);
    const std::string b64_string = std::string(b64_bytes.data(), b64_bytes.size());

    ConfFileTemp config;
    config.writeLine(b64_string);
    config.closeFile();

    ConfigFileParser parser(config.getFilePath());
    parser.loadFile(false);

    Settings settings = parser.getSettings();

    std::list<std::shared_ptr<BridgeConfig>> bridges = settings.stealBridges();
    MYCASTCOMPARE(bridges.size(), 1);

    std::shared_ptr<BridgeConfig> &bridge = bridges.front();

    FMQ_COMPARE(bridge->subscribes.at(0).topic, "asdfasdfee/wer/+");
    FMQ_COMPARE(bridge->subscribes.at(0).qos, (uint8_t)2);
    FMQ_COMPARE(bridge->local_username, "abcde#fAEBF4_-/:+");
    FMQ_COMPARE(bridge->clientidPrefix, "zH53_-::_");
}






