#include "WebAuthentication.h"
#include <string.h>
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"

#define base64_cacl_len(x)  (((x + 2) / 3) * 4)

/// @brief 检查基本认证是否正确（规则为hash==base64("username:password")）
/// @param hash 传入的哈希值
/// @param username 用户名
/// @param password 密码
/// @return 认证成功返回true
bool checkBasicAuthentication(const char * hash, const char * username, const char * password)
{

    if (username == nullptr || password == nullptr || hash == nullptr) {
        return false;
    }

    // 比较长度
    size_t toencodeLen = strlen(username) + strlen(password) + 1; // 要编码的长度
    size_t encodedLen = base64_cacl_len(toencodeLen);              // 编码后的长度
    if (strlen(hash) != encodedLen) {
        return false;
    }

    char *toencode = new char[toencodeLen + 1];
    if (toencode == nullptr) {
        return false;
    }
    char *encoded = new char[encodedLen + 1];
    if (encoded == nullptr) {
        delete[] toencode;
        return false;
    }
    sprintf(toencode, "%s:%s", username, password);
    size_t output_len = encodedLen;
    int ret = mbedtls_base64_encode((unsigned char*)encoded, toencodeLen + 1, &output_len, (unsigned char*)toencode, toencodeLen);
    if (ret == 0 && memcmp(hash, encoded, encodedLen) == 0) {
        delete[] toencode;
        delete[] encoded;
        return true;
    }
    delete[] toencode;
    delete[] encoded;
    return false;
}

/// @brief 计算指定数据的MD5值
/// @param data 输入数据指针
/// @param len 输入数据长度
/// @param output MD5值班输出u位置
/// @return 输出成功返回true
static bool getMD5(uint8_t * data, uint16_t len, char * output)
{
    mbedtls_md5_context _ctx;

    uint8_t i;
    uint8_t* _buf = new uint8_t[16];      // MD5长度为16字节
    if (_buf == nullptr) {
        return false;
    }
    memset(_buf, 0x00, 16);

    mbedtls_md5_init(&_ctx);                // md5计算步骤1.1：初始化上下文
    mbedtls_md5_starts(&_ctx);              // md5计算步骤1.2：开始计算
    mbedtls_md5_update(&_ctx, data, len);   // md5计算步骤2：逐块更新数据
    mbedtls_md5_finish(&_ctx, _buf);        // md5计算步骤3：获取最终哈希值

    for (i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", _buf[i]);
    }
    delete[] _buf;
    return true;
}

/// @brief 获取一个随机的MD5值
/// @return MD5值
static std::string genRandomMD5()
{
    uint32_t r = rand();
    char* out = new char[33];     // MD5为16位二进制，其转化为字条串需33字节
    if (out == nullptr || !getMD5((uint8_t *)(&r), 4, out)) {
        return "";
    }
    std::string str = std::string(out);
    delete[] out;
    return str;
}

/// @brief 计算指定字符串的MD5值
/// @param in 输入字符串
/// @return 成功返回MD5值，失败返回空字符串
static std::string stringMD5(const std::string &in)
{
    char* out = new char[33];
    if (out == nullptr || !getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
        return empty_string;
    }
    std::string str = std::string(out);
    delete[] out;
    return str;
}

/// @brief
/// @param username
/// @param password
/// @param realm 服务器告知客户端的一个字符串，用于定义一组需要相同认证信息的资源
/// @return
std::string generateDigestHash(const char * username, const char * password, const char * realm)
{
    if (username == nullptr || password == nullptr || realm == nullptr) {
        return empty_string;
    }
    char* out = new char[33];
    std::string res = std::string(username);
    res += ':';
    res += std::string(realm);
    res += ':';
    res += std::string(password);

    std::string in = res;
    if (out == nullptr || !getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
        return empty_string;
    }
    res += out;
    delete[] out;
    return res;
}

std::string requestDigestAuthentication(const char * realm)
{
    std::string header = R"(realm=")";
    if (realm == nullptr) {
        header += R"(asyncesp)";
    } else {
        header += std::string(realm);
    }
    header += R"(", qop="auth", nonce=")";
    header += genRandomMD5();
    header += R"(", opaque=")";
    header += genRandomMD5();
    header += R"(")";
    return header;
}

/// @brief
/// @param header 请求中的Authorization头部信息
/// @param method 请求的方法
/// @param username 预期的用户名
/// @param password 用户密码
/// @param realm 服务器字符串
/// @param passwordIsHash 密码是否为哈希值
/// @param nonce 由服务器生成的一次性数值，防重放
/// @param opaque
/// @param uri 客户端请求的 URI
/// @return
bool checkDigestAuthentication(const char * header, const char * method, const char * username,
                               const char * password, const char * realm, bool passwordIsHash, const char * nonce,
                               const char * opaque, const char * uri)
{
    if (username == nullptr || password == nullptr || header == nullptr || method == nullptr) {
        return false;
    }

    std::string myHeader = std::string(header);
    size_t nextBreak = myHeader.find(',');
    if (nextBreak == std::string::npos) {
        return false;
    }

    std::string myUsername = empty_string;
    std::string myRealm = empty_string;
    std::string myNonce = empty_string;
    std::string myUri = empty_string;
    std::string myResponse = empty_string;
    std::string myQop = empty_string;
    std::string myNc = empty_string;
    std::string myCnonce = empty_string;

    myHeader += ", ";
    do {
        std::string avLine = myHeader.substr(0, nextBreak);
        // avLine.trim();
        avLine.erase(avLine.begin(), std::find_if(avLine.begin(), avLine.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        avLine.erase(std::find_if(avLine.rbegin(), avLine.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), avLine.end());
        myHeader = myHeader.substr(nextBreak + 1);
        nextBreak = myHeader.find_first_of(',');

        int eqSign = avLine.find_first_of('=');
        if (eqSign < 0) {
            return false;
        }
        std::string varName = avLine.substr(0, eqSign);
        avLine = avLine.substr(eqSign + 1);
        if (avLine.starts_with("\"")) {
            avLine = avLine.substr(1, avLine.length() - 1);
        }

        if (varName == "username") {
            if (avLine != username) {
                return false;
            }
            myUsername = avLine;
        } else if (varName == "realm") {
            if (realm != nullptr && avLine != realm) {
                return false;
            }
            myRealm = avLine;
        } else if (varName == "nonce") {
            if (nonce != nullptr && avLine != nonce) {
                return false;
            }
            myNonce = avLine;
        } else if (varName == "opaque") {
            if (opaque != nullptr && avLine != opaque) {
                return false;
            }
        } else if (varName == "uri") {
            if (uri != nullptr && avLine != uri) {
                return false;
            }
            myUri = avLine;
        } else if (varName == "response") {
            myResponse = avLine;
        } else if (varName == "qop") {
            myQop = avLine;
        } else if (varName == "nc") {
            myNc = avLine;
        } else if (varName == "cnonce") {
            myCnonce = avLine;
        }
    } while (nextBreak > 0);

    std::string ha1 = (passwordIsHash) ? std::string(password) : stringMD5(myUsername + ":" + myRealm + ":" + std::string(password));
    std::string ha2 = std::string(method) + ":" + myUri;
    std::string response = ha1 + ":" + myNonce + ":" + myNc + ":" + myCnonce + ":" + myQop + ":" + stringMD5(ha2);

    if (myResponse == stringMD5(response)) {
        return true;
    }

    return false;
}
