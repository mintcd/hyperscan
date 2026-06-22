#ifndef EXAMPLES_UTILS_STRING_PROCESSING_H
#define EXAMPLES_UTILS_STRING_PROCESSING_H

#include <string>

namespace ue2 {
class CharReach;
struct ue2_literal;
}

std::string formatCharReachSimple(const ue2::CharReach &cr);
std::string escapeJsonString(const std::string &input);
std::string formatLiteral(const ue2::ue2_literal &lit);

void create_directories(const std::string &path);

#endif // EXAMPLES_UTILS_STRING_PROCESSING_H
