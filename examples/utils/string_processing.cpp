#include "string_processing.h"
#include "util/charreach.h"

#include <sstream>
#include <iomanip>
#include <cctype>

std::string formatCharReachSimple(const ue2::CharReach &cr) {
	using ue2::CharReach;
	if (cr.none()) {
		return "none";
	}
	if (cr.all()) {
		return "any";
	}

	std::ostringstream os;
	bool first = true;
	for (size_t i = cr.find_first(); i != CharReach::npos;) {
		size_t j = i;
		while (true) {
			size_t n = cr.find_next(j);
			if (n == CharReach::npos || n != j + 1) {
				break;
			}
			j = n;
		}

		if (!first) {
			os << ",";
		}
		first = false;

		if (i == j) {
			unsigned c = (unsigned)i;
			if (isprint(c) && c != '\\' && c != '"') {
				os << (char)c;
			} else {
				os << "0x" << std::hex << std::setw(2) << std::setfill('0') << c << std::dec;
			}
		} else {
			os << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned)i
			   << "-0x" << std::setw(2) << (unsigned)j << std::dec;
		}

		i = cr.find_next(j);
	}

	return os.str();
}

std::string escapeJsonString(const std::string& input) {
	std::ostringstream ss;
	for (unsigned char c : input) {
		if (c == '"') ss << "\\\"";
		else if (c == '\\') ss << "\\\\";
		else if (c == '\b') ss << "\\b";
		else if (c == '\f') ss << "\\f";
		else if (c == '\n') ss << "\\n";
		else if (c == '\r') ss << "\\r";
		else if (c == '\t') ss << "\\t";
		else if (c < 0x20) {
			ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
		} else {
			ss << c;
		}
	}
	return ss.str();
}

