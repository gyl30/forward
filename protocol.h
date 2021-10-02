#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <inttypes.h>
#include <stdint.h>
#include <memory>
#include <vector>
#include <assert.h>
#include "log.h"
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
    memcpy(&x, ptr, sizeof x);
    return x;
}
using SharedVector = std::shared_ptr<std::vector<uint8_t>>;
static SharedVector make_shard_vector(std::size_t size) { return std::make_shared<std::vector<uint8_t>>(size, '0'); }
static SharedVector make_shard_vector(const std::string& msg)
{
    return std::make_shared<std::vector<uint8_t>>(msg.begin(), msg.end());
}

struct FixedSizeCodec
{
    static SharedVector encode(const std::string& m)
    {
        auto msg = make_shard_vector(m);
        std::size_t msg_size = msg->size();
        auto v = make_shard_vector(MsgPkg::kHeadSize + msg_size);
        auto x = MsgPkg::hostToNetwork32(msg_size);
        // LOG_DEBUG << "encode msg size " << msg_size << " to network " << x;
        memcpy(v->data(), &x, MsgPkg::kHeadSize);
        std::copy(msg->begin(), msg->end(), v->begin() + MsgPkg::kHeadSize);
        return v;
    }
    static std::string decode(const SharedVector& msg)
    {
        std::size_t msg_size = msg->size();
        auto x = MsgPkg::networkToHost32(MsgPkg::peek_uint32_t(msg->data()));
        // LOG_DEBUG << "decode network msg size " << msg_size << " to host " << x;
        assert(x == msg_size - MsgPkg::kHeadSize);
        return std::string(msg->begin() + MsgPkg::kHeadSize, msg->end());
    }
    static uint32_t body_size(const SharedVector& buff)
    {
        return MsgPkg::networkToHost32(MsgPkg::peek_uint32_t(buff->data()));
    }
};
struct BufferSizeCodec
{
    static SharedVector encode(const std::string& msg)
    {
        std::size_t msg_size = msg.size();
        char buf[MsgPkg::kHeadSize + 1] = {0};
        sprintf(buf, "%4d", static_cast<int>(msg_size));
        auto v = make_shard_vector(MsgPkg::kHeadSize + msg_size);
        memcpy(v->data(), buf, MsgPkg::kHeadSize);
        // LOG_DEBUG << "encode size " << buf;
        std::copy(msg.begin(), msg.end(), v->begin() + MsgPkg::kHeadSize);
        return v;
    }
    static std::string decode(const SharedVector& msg)
    {
        char buf[MsgPkg::kHeadSize + 1] = {0};
        ::strncat(buf, (char*)msg->data(), MsgPkg::kHeadSize);
        int msg_size = ::atoi(buf);
        // LOG_DEBUG << "decode size " << buf;
        assert(msg_size == msg->size() - MsgPkg::kHeadSize);
        return std::string(msg->begin() + MsgPkg::kHeadSize, msg->end());
    }
    static uint32_t body_size(const SharedVector& buff)
    {
        char buf[MsgPkg::kHeadSize + 1] = {0};
        ::strncat(buf, (char*)buff->data(), MsgPkg::kHeadSize);
        return ::atoi(buf);
    }
};
//#define BUFFER_SIZE_CODEC 1
struct codec
{
#if BUFFER_SIZE_CODEC
    static SharedVector encode(const std::string& m) { return BufferSizeCodec::encode(m); }
    static std::string decode(const SharedVector& msg) { return BufferSizeCodec::decode(msg); }
    static uint32_t body_size(const SharedVector& buff) { return BufferSizeCodec::body_size(buff); }
#else
    static SharedVector encode(const std::string& m) { return FixedSizeCodec::encode(m); }
    static std::string decode(const SharedVector& msg) { return FixedSizeCodec::decode(msg); }
    static uint32_t body_size(const SharedVector& buff) { return FixedSizeCodec::body_size(buff); }
#endif
};

}    // namespace MsgPkg

#endif    //__PROTOCOL_H__
