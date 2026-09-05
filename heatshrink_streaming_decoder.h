/*
 * Decodes a heatshrink source without input buffering. Outputs by byte or by sector.
 * Copyright (C) 2026, Lucas V. Hartmann <github.com/lhartmann>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdint.h>
#include <optional>
#include <functional>
#include <cstring>

namespace heatshrink_streaming {

struct HS_ROM_Reader {
    const uint8_t *source;
    size_t source_size;

    const uint8_t *p;

    void reset() {
        p = source;
    }

    HS_ROM_Reader(const uint8_t *p, size_t len) {
        source = p;
        source_size = len;

        reset();
    }

    std::optional<uint8_t> operator()() {
        if (p - source == source_size)
            return std::nullopt;

        return *p++;
    }
};

struct HS_ifstream_Reader {
    std::ifstream &in;

    void reset() {
        in.seekg(0);
    }

    HS_ifstream_Reader(std::ifstream &in_) : in(in_) {
        reset();
    }

    std::optional<uint8_t> operator()() {
        char ch;
        if (!in.get(ch))
            return std::nullopt;

        return ch;
    }
};

template <class Reader, unsigned Wbits, unsigned Lbits>
struct HS_Decoder {
    static constexpr uint32_t Wmask = (1U << Wbits) - 1U;
    static constexpr uint32_t Lmask = (1U << Lbits) - 1U;

    // User supplied
    Reader reader;

    // Internal states. Initial = expecting 1 bit for tag.
    uint8_t input_buffer;
    uint8_t input_mask;
    uint8_t pending_bits;
    uint8_t backref_bytes;
    bool    expecting_tag;
    bool    expecting_literal;

    uint16_t bits;
    uint16_t backref_offset;
    uint16_t buffer_head;

    uint8_t  buffer[1 << Wbits];

    HS_Decoder(Reader r) : reader(r) {
        reset();
    }

    void reset() {
        input_buffer = 0;
        input_mask = 0;
        pending_bits = 1;
        backref_bytes = 0;
        expecting_tag = true;
        expecting_literal = false;

        bits = 0;
        backref_offset = 0;
        buffer_head = 0;

        memset(buffer, 0, sizeof(buffer));

        // If getter has a reset method, call it
        if constexpr (requires { reader.reset(); }) {
            reader.reset();
        }
    }

    void buffer_push(uint8_t b) {
        buffer[buffer_head++] = b;
        buffer_head &= Wmask;
    }

    std::optional<bool> getbit() {
        if (!input_mask) {
            auto byte = reader();
            if (!byte) return std::nullopt;

            input_buffer = *byte;
            input_mask = 0x80;
        }

        bool bit = input_buffer & input_mask;
        input_mask >>= 1;
        return bit;
    }

    std::optional<uint8_t> get() {
        if (backref_bytes) {
            uint8_t b = buffer[(buffer_head - backref_offset) & Wmask];
            if (--backref_bytes == 0) {
                pending_bits = 1;
                expecting_tag = true;
            }

            buffer_push(b);
            return b;
        }

        while (pending_bits) {
            auto bit = getbit();
            if (!bit)
                return std::nullopt;

            bits = (bits<<1) | *bit;
            pending_bits--;
        }

        if (expecting_literal) {
            expecting_literal = false;

            uint8_t b = bits & 0xFF;
            buffer_push(b);

            pending_bits = 1;
            expecting_tag = true;

            return b;

        } else if (expecting_tag) {
            expecting_tag = false;

            if (bits & 1) {
                pending_bits = 8;
                expecting_literal = true;
            } else {
                pending_bits = Wbits + Lbits;
            }

        } else {
            backref_offset = ((bits >> Lbits) & Wmask) + 1;
            backref_bytes = (bits & Lmask) + 1;
        }

        return get(); // Tailcall, takes no stack.
    }

    const uint8_t *get_sector() {
        if (!buffer_head)
            if (!get())
                return 0;

        while (buffer_head)
            if (!get())
                return 0;

        return buffer;
    }

    const uint8_t *data() {
        return buffer;
    }
    size_t size() {
        if (buffer_head == 0)
            return Wmask + 1;
        return buffer_head;
    }
};

template <unsigned W, unsigned L, class Reader>
static auto makeDecoder(Reader reader) {
    return HS_Decoder<Reader,W,L>(reader);
}

} // namespace heatshrink_streaming_decoder
