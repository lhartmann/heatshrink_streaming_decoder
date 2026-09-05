#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <stdint.h>
#include <cstdlib>
#include <ctime>

using namespace std;

#include "heatshrink_streaming_decoder.h"

template <class HS_Decoder>
const uint8_t *read_sector(HS_Decoder &hssd, size_t sector) {
    size_t retries = 1000000;

    // Rewind decoder
    hssd.reset();

    // Skip undesired sectors
    while (sector && retries) {
        const uint8_t *p = hssd.get_sector();
        if (p)
            sector--;

        retries--;
    }

    while (retries) {
        const uint8_t *p = hssd.get_sector();
        if (p)
            return p;

        retries--;
    }

    return 0;
}

template <class HS_Decoder>
void test_bytewise(HS_Decoder &hssd, std::ostream &outfile, std::function<bool()> done) {
    size_t retries = 1000000;
    while (retries-- && !done()) {
        auto b = hssd.get();
        if (b)
            outfile.put(*b);
        // else if (!infile)
        //     break;
    }
}

template <class HS_Decoder>
void test_sectorwise(HS_Decoder &hssd, std::ostream &outfile, std::function<bool()> done) {
    size_t retries = 10000000;
    while (retries-- && !done()) {
        const uint8_t *p = hssd.get_sector();
        if (p)
            outfile.write((const char*)p, hssd.Wmask+1);
        // else if (!infile)
        //     break;
    }
}

template <class HS_Decoder>
void test_reset_skip_sectorwise(HS_Decoder &hssd, std::ostream &outfile, std::function<bool()> done) {
    size_t retries = 1000;
    size_t sector = 0;

    while (retries-- && !done()) {
        const uint8_t *p = read_sector(hssd, sector);
        if (p) {
            outfile.write((const char*)p, hssd.Wmask+1);
            sector++;
        }
        // else if (!infile)
        //     break;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        cout << "Usage:" << endl;
        cout << argv[0] << " {-b|-s|-r} <infile.hs> <outfile.raw>" << endl;
        return 0;
    }

    string input_name  = argv[2];
    string output_name = argv[3];
    string mode = argv[1];

    srand(time(0));

    ifstream infile(input_name.c_str(), ios::in | ios::binary);

    // Use istream rader (can reset)
    // auto hssd = make_hssd<9,5>(HS_ifstream_Reader(infile));

    // Use lambda reader (can not reset)
    auto hssd = heatshrink_streaming::makeDecoder<9,5>([&infile]() -> std::optional<uint8_t> {
        // Simulate input not-ready.
        if (rand() < RAND_MAX/2)
            return std::nullopt;

        // Normal behavior
        char ch;
        if (infile.get(ch))
            return ch;
        return std::nullopt;
    });

    ofstream outfile(output_name.c_str(), ios::out | ios::binary | ios::trunc);

    auto done = [&]() -> bool {
        return !infile;
    };

    if (mode == "-b") {
        test_bytewise(hssd, outfile, done);
    } else if (mode == "-s") {
        test_sectorwise(hssd, outfile, done);
    } else if (mode == "-r") {
        test_reset_skip_sectorwise(hssd, outfile, done);
    } else {
        cout << "Invalid mode." << endl;
        return 1;
    }
}
