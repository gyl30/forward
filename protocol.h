#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

namespace protocol
{
std::string encode_header(uint32_t msg_size)
{
    char buf[5] = {0};
    sprintf(buf, "%04d", static_cast<int>(msg_size));
    return buf;
}
uint32_t decode_header(const std::string& msg)
{
    char buf[5] = {0};
    ::strncat(buf, (char*)msg.data(), 4);
    return ::atoi(buf);
}
}    // namespace protocol

#endif    // __PROTOCOL_H__
