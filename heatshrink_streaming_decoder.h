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

template <unsigned Wbits=9, unsigned Lbits=5>
struct heatshrink_streaming_decoder {
    static constexpr uint32_t Wmask = (1U << Wbits) - 1U;
    static constexpr uint32_t Lmask = (1U << Lbits) - 1U;

    // User supplied
    std::function<std::optional<uint8_t>()> getbyte;

    // Internal states. Initial = expecting 1 bit for tag.
    uint8_t input_buffer{0};
    uint8_t input_mask{0};
    uint8_t pending_bits{1};
    uint8_t backref_bytes{0};
    bool    expecting_tag{true};
    bool    expecting_literal{false};

    uint16_t bits{0};
    uint16_t backref_offset{0};
    uint16_t buffer_head{0};

    uint8_t  buffer[1 << Wbits] = {0};

    void buffer_push(uint8_t b) {
        buffer[buffer_head++] = b;
        buffer_head &= Wmask;
    }

    std::optional<bool> getbit() {
        if (!input_mask) {
            auto byte = getbyte();
            if (!byte) return std::nullopt;

            input_buffer = *byte;
            input_mask = 0x80;
        }

        bool bit = input_buffer & input_mask;
        input_mask >>= 1;
        return bit;
    }

    std::optional<uint8_t> pop() {
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

        return pop(); // Tailcall, takes no stack.
    }

    const uint8_t *pop_sector() {
        if (!buffer_head)
            if (!pop())
                return 0;

        while (buffer_head)
            if (!pop())
                return 0;

        return buffer;
    }
};

