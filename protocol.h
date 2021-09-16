#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>
#ifdef __MACH__
#include <libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#else
#include <endian.h>
#endif

namespace MsgPkg
{
const static auto kHeadSize = sizeof(uint32_t);
inline uint32_t networkToHost32(uint32_t net32) { return be32toh(net32); }
inline uint32_t hostToNetwork32(uint32_t host32) { return htobe32(host32); }
inline uint32_t peek_uint32_t(void* ptr)
{
    uint32_t x = 0;
    memcpy(ptr, &x, sizeof x);
    return x;
}

struct codec
{
    using SharedVector = std::shared_ptr<std::vector<uint8_t>>;
    static SharedVector make_shard_vector(std::size_t size)
    {
        return std::make_shared<std::vector<uint8_t>>(size, '0');
    }
    static SharedVector make_shard_vector(const std::string& msg)
    {
        return std::make_shared<std::vector<uint8_t>>(msg.begin(), msg.end());
    }

    static SharedVector make_encode_shard_vector(const std::string& msg)
    {
        return make_encode_shard_vector(make_shard_vector(msg));
    }
    static SharedVector make_encode_shard_vector(const SharedVector& msg)
    {
        auto x = MsgPkg::hostToNetwork32(msg->size());
        auto v = make_shard_vector(MsgPkg::kHeadSize + msg->size());
        const char* d = (const char*)&x;
        std::copy(d, d + MsgPkg::kHeadSize, v->begin());
        std::copy(msg->begin(), msg->end(), v->begin() + MsgPkg::kHeadSize);
        return v;
    }
    static std::string make_decode_shard_vector(const SharedVector& msg)
    {
        auto x = MsgPkg::networkToHost32(MsgPkg::peek_uint32_t(msg->data()));
        assert(x == msg->size() - MsgPkg::kHeadSize);
        return std::string(msg->begin() + MsgPkg::kHeadSize, msg->end());
    }
};

}    // namespace MsgPkg

#endif    //__PROTOCOL_H__
