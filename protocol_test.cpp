#include "protocol.h"
#include <assert.h>

int main(int argc, char *argv[])
{
    std::string h = protocol::encode_header(5);
    assert(h == "0005");

    uint32_t size = protocol::decode_header(h);
    assert(size == 5);
    return 0;
}
