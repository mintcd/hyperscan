#ifndef EXAMPLES_UTILS_STRING_PROCESSING_H
#define EXAMPLES_UTILS_STRING_PROCESSING_H

#include <string>

namespace ue2 { class CharReach; }

std::string formatCharReachSimple(const ue2::CharReach &cr);
std::string escapeJsonString(const std::string &input);

#endif // EXAMPLES_UTILS_STRING_PROCESSING_H
