#ifndef TOOLS_H_
#define TOOLS_H_

#include <string>

extern const std::string empty_string;
extern bool equalsIgnoreCase(std::string str1, std::string str2);
extern bool FILE_IS_REAL(const char* path);
extern bool FILE_EXISTS(const char* path);
extern bool strContains(std::string src, std::string find, bool ignoreCase=true);


#endif // !TOOLS_H_