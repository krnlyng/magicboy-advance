// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Franz-Josef Haider

#include <Magick++.h>
#include <iostream>
#include <iomanip>

#include <stdint.h>
#define u8 uint8_t
#define u16 uint16_t

using namespace Magick;
using namespace std;

int main(int argc, char *argv[])
{
    if (argc != 4) {
        cout << "Usage: " << argv[0] << " numbers.png fontname color" << endl;
        return -1;
    }

    char *infile = argv[1];
    char *fontname = argv[2];
    char *strcolor = argv[3];
    int color = strtol(strcolor, NULL, 16);

    try {
        InitializeMagick(*argv);
        Image img(infile);
        int w = img.columns();
        int h = img.rows();
        uint32_t sizeinbytes = 0;
        cout << "#include <stdint.h>" << endl;
        cout << "#define u16 uint16_t" << endl;
        cout << "#define u32 uint32_t" << endl;
        cout << "const u16 " << fontname << "[] __attribute__ ((section (\".rodata\"))) = {";
        for (int j = 0; j < img.rows(); j++) {
            for (int i = 0; i < img.columns(); i++) {
                ColorRGB c = img.pixelColor(i, j);
                int outcolor = 0;
                if ((int)c.red() == 0) {
                    outcolor = color;
                }
                cout << hex << "0x" << outcolor << ", ";
                sizeinbytes += 2;
            }
            cout << endl;
        }
        cout << "};" << endl;
        cout << "const u32 " << fontname << "_SIZE = 0x" << sizeinbytes << ";" << endl;
    } catch(Magick::Exception & error) {
        cerr << "Caught Magick++ exception: " << error.what() << endl;
    }
}

