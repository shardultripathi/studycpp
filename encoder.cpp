#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// An unsigned integer is encoded into a stream with a variable number of bytes
// with the help of a stop-bit (which is the MSB of the byte). So the stop-bit
// of only the last byte in the encoded sequence is set. The meaningful bits of
// the given integer are then cut into chunks and written to the meaningful 7
// bits of the encoded bytes, so that most significant bits of the integer go to
// the first byte. For example the integers in the range [0, 127] are encoded in
// just 1 byte, and encoded sequence for value 128 will require 2 bytes (0x01
// 0x80).
//
// Implement an encoder and a decoder for all unsigned types with proper error
// handling for the cases when the input is incorrect. If the given buffer is
// too small to store a result no data should be written there. The functions
// should not compile for any other types, for example, signed int. It is
// permitted to reformulate the provided function signatures in order to ensure
// that they only participate in overload resolution if UInt is an unsigned
// integer type, as long as the test cases compile without modification.
//
// Example 1
//               76543210 76543210
// Encoded value 00000001 10000000 = (0x01 0x80)
// Decoded value  0000001  0000000 = 128
//
// Example 2
//               76543210 76543210 76543210
// Encoded value 00000001 00011010 10010010 = (0x01 0x1A 0x92)
// Decoded value  0000001  0011010  0010010 = 19730

template <typename T>
concept Unsigned = std::is_unsigned_v<T>;

// encodes the value into a buffer of a given length
// returns number of the bytes in the encoded string
template <Unsigned UInt>
std::size_t encodeInteger(UInt val, char* buf, std::size_t len) {
    // Number of encoded bytes
    size_t encoded_bytes = 1;
    UInt tmp = val >> 7;
    while (tmp) {
        ++encoded_bytes;
        tmp >>= 7;
    }

    // Check if buffer is large enough
    if (!buf || encoded_bytes > len) {
        return 0;
    }

    // Perform encoding
    constexpr uint8_t mask = 0x7F;      // 0111 1111
    constexpr uint8_t stop_bit = 0x80;  // 1000 0000
    int pos = encoded_bytes - 1;

    while (pos >= 0) {
        buf[pos--] = val & mask;
        val >>= 7;
    }
    buf[encoded_bytes - 1] |= stop_bit;  // Set the stop bit
    return encoded_bytes;
}

// returns the decoded value
template <Unsigned UInt>
UInt decodeInteger(const char* buf, std::size_t len) {
    // Validate basic input
    if (!buf || len == 0 || len > sizeof(UInt) * 8 / 7 + 1) {
        return 0;
    }

    // Validate stop bits
    constexpr uint8_t stop_bit = 0x80;  // 1000 0000
    for (size_t i = 0; i < len - 1; i++) {
        if (buf[i] & stop_bit) {
            return 0;  // Invalid: stop bit in middle of sequence
        }
    }
    if (!(buf[len - 1] & stop_bit)) {
        return 0;  // Invalid: missing stop bit at end
    }

    // Perform decoding
    constexpr uint8_t mask = 0x7F;  // 0111 1111
    UInt val = 0;
    for (size_t i = 0; i < len; ++i) {
        val |= (buf[i] & mask) << (7 * (len - i - 1));
    }

    return val;
}

template <typename UInt>
void testEncodeDecode(UInt val, const char* v, std::size_t vlen) {
    UInt dec = decodeInteger<UInt>(v, vlen);
    assert(dec == val);

    char enc[10];
    std::size_t encLen = encodeInteger<UInt>(val, enc, sizeof(enc));
    assert(encLen == vlen);
    assert(!std::strncmp(enc, v, vlen));
}

int main() {
    testEncodeDecode<unsigned>(128, "\x01\x80", 2);
    testEncodeDecode<unsigned>(19730, "\x01\x1A\x92", 3);
    testEncodeDecode<unsigned>(16, "\x90", 1);  // single byte encoding
    // testEncodeDecode<unsigned>(128, "\x01\x00", 2); // stop bit failed
    // testEncodeDecode<int>(19730, "\x01\x1A\x92", 3); // should not compile

    return 0;
}
