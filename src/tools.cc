#include "tools.h"
#include <string>
#include <stdio.h>
#include <sys/stat.h>

// 构造空对象需要时间，这里构造一个供整个库使用
const std::string empty_string = std::string();


bool FILE_IS_REAL(const char* path)
{
    struct stat path_stat;
    if (stat(path, &path_stat) == -1) {
        return false;
    }
    if (S_ISDIR(path_stat.st_mode)) {
        return false;
    }
    return true;
}

bool FILE_EXISTS(const char* path)
{
    struct stat path_stat;
    return stat(path, &path_stat) == -1 ? false : true;
}

bool strContains(std::string src, std::string find, bool ignoreCase)
{
    size_t pos = 0, i = 0;
    const size_t slen = src.length();
    const size_t flen = find.length();

    if (slen < flen) {
        return false;
    }
    while (pos <= (slen - flen)) {
        for (i = 0; i < flen; i++) {
            if (ignoreCase) {
                if (src[pos + i] != find[i]) {
                    i = flen + 1;    // no match
                }
            } else if (tolower(src[pos + i]) != tolower(find[i])) {
                i = flen + 1;    // no match
            }
        }
        if (i == flen) {
            return true;
        }
        pos++;
    }
    return false;
}