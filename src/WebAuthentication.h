#ifndef WEB_AUTHENTICATION_H_
#define WEB_AUTHENTICATION_H_

#include "StringArray.h"

bool checkBasicAuthentication(const char * header, const char * username, const char * password);
std::string requestDigestAuthentication(const char * realm);
bool checkDigestAuthentication(const char * header, const char * method, const char * username, const char * password, const char * realm, bool passwordIsHash, const char * nonce, const char * opaque, const char * uri);

//for storing hashed versions on the device that can be authenticated against
std::string generateDigestHash(const char * username, const char * password, const char * realm);

#endif