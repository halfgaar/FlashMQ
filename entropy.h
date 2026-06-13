#ifndef ENTROPY_H
#define ENTROPY_H

#include <string>

double entropy(const std::string& s);
double repetitionPenalty(const std::string& s);
double alternatingPatternPenalty(const std::string& s);
bool isRandomEnough(const std::string& s, double threshold = 0.75);

#endif // ENTROPY_H
