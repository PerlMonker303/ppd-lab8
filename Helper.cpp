#include "Helper.h"

bool Helper::findInVector(std::vector<int> vec, int elem) {
    for (auto v : vec) {
        if (v == elem) {
            return true;
        }
    }
    return false;
}