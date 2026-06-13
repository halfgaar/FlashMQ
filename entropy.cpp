#include <unordered_map>
#include <cmath>
#include <algorithm>
#include "entropy.h"

// AI generated, plus tweaked
double entropy(const std::string& s)
{
    if (s.empty())
        return 0.0;

    std::unordered_map<char, int> freq;
    for (char c : s)
        freq[c]++;

    double ent {};
    for (auto& [ch, count] : freq)
    {
        double p {static_cast<double>(count) / s.size()};
        ent -= p * std::log2(p);
    }

    const double maxEntropy = std::log2(freq.size());
    return maxEntropy > 0 ? ent / maxEntropy : 0.0;
}

// AI generated, plus tweaked
double repetitionPenalty(const std::string& s)
{
    int repeats = 0;
    for (size_t i = 1; i < s.size(); i++)
    {
        if (s.at(i) == s.at(i - 1))
            repeats++;
    }
    return static_cast<double>(repeats) / std::max<size_t>(1, s.size() - 1);
}

// AI generated, plus tweaked
double alternatingPatternPenalty(const std::string& s)
{
    int alt = 0;
    for (size_t i = 2; i < s.size(); i++)
    {
        if (s.at(i) == s.at(i - 2))
            alt++;
    }
    return static_cast<double>(alt) / std::max<size_t>(1, s.size() - 2);
}

// AI generated, plus tweaked
bool isRandomEnough(const std::string& s, double threshold)
{
    if (s.size() < 8)
        return false;

    const double h {entropy(s)};
    const double rep {repetitionPenalty(s)};
    const double alt {alternatingPatternPenalty(s)};

    double score {h * (1.0 - rep) * (1.0 - alt)};

    return score >= threshold;
}
