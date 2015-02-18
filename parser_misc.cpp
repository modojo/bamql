#include <stdlib.h>
#include <sstream>
#include "barf.hpp"

namespace barf {
parse_error::parse_error(size_t index, std::string what)
    : std::runtime_error(what) {
  this->index = index;
}
size_t parse_error::where() { return index; }

int parse_int(const std::string &input, size_t &index) throw(parse_error) {
  size_t start = index;
  int accumulator = 0;
  while (index < input.length() && input[index] >= '0' && input[index] <= '9') {
    accumulator = 10 * accumulator + (input[index] - '0');
    index++;
  }
  if (start == index) {
    throw parse_error(start, "Expected integer.");
  }
  return accumulator;
}
double parse_double(const std::string &input,
                    size_t &index) throw(parse_error) {
  size_t start = index;
  char *end_ptr = nullptr;
  auto result = strtod(input.c_str() + start, &end_ptr);
  index = end_ptr - input.c_str();
  if (index == start) {
    throw parse_error(index, "Expected floating point number.");
  }
  return result;
}
std::string parse_str(const std::string &input,
                      size_t &index,
                      const std::string &accept_chars,
                      bool reject) throw(parse_error) {
  size_t start = index;
  while (index < input.length() &&
         ((accept_chars.find(input[index]) != std::string::npos) ^ reject)) {
    index++;
  }
  if (start == index) {
    throw parse_error(start, "Unexpected character.");
  }
  return input.substr(start, index - start);
}
bool parse_space(const std::string &input, size_t &index) {
  size_t start = index;
  while (index < input.length() &&
         (input[index] == ' ' || input[index] == '\t' || input[index] == '\n' ||
          input[index] == '\r')) {
    index++;
  }
  return start != index;
}
void parse_char_in_space(const std::string &input,
                         size_t &index,
                         char c) throw(parse_error) {
  parse_space(input, index);
  if (index >= input.length() || input[index] != c) {
    std::ostringstream err_text;
    err_text << "Expected `" << c << "'.";
    throw parse_error(index, err_text.str());
  }
  index++;
  parse_space(input, index);
}
bool parse_keyword(const std::string &input,
                   size_t &index,
                   const std::string &keyword) {
  for (size_t it = 0; it < keyword.length(); it++) {
    if (index + it >= input.length() || input[index + it] != keyword[it]) {
      return false;
    }
  }
  index += keyword.length();
  return true;
}
}
