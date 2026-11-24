// Tables du format CTG (bibliothèques d'ouvertures ChessBase), issues de ctgreader
// (https://github.com/sshivaji/ctgreader, licence MIT, © 2016 Shivkumar Shivaji), lui-même
// fondé sur le code CTG de Stephan Vermeire pour le moteur Brutus.
//
// Licence de ctgreader :
//   The MIT License (MIT)
//
//   Copyright (c) 2016 Shivkumar Shivaji
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// Un coup tient sur un octet : type de pièce (PieceCode), rang de la pièce parmi celles de ce
// type en parcourant l'échiquier colonne par colonne depuis a1 (PieceIndex), déplacement vers
// l'avant (Forward) et vers la gauche (Left), vus du camp au trait.

constexpr char PieceCode[] =
    "PNxQPQPxQBKxPBRNxxBKPBxxPxQBxBxxxRBQPxBPQQNxxPBQNQBxNxNQQQBQBxxx"
    "xQQxKQxxxxPQNQxxRxRxBPxxxxxxPxxPxQPQxxBKxRBxxxRQxxBxQxxxxBRRPRQR"
    "QRPxxNRRxxNPKxQQxxQxQxPKRRQPxQxBQxQPxRxxxRxQxRQxQPBxxRxQxBxPQQKx"
    "xBBBRRQPPQBPBRxPxPNNxxxQRQNPxxPKNRxRxQPQRNxPPQQRQQxNRBxNQQQQxQQx";

constexpr int8_t PieceIndex[256] = {
     5,  2,  9,  2,  2,  1,  4,  9,  2,  2,  1,  9,  1,  1,  2,  1,
     9,  9,  1,  1,  8,  1,  9,  9,  7,  9,  2,  1,  9,  2,  9,  9,
     9,  2,  2,  2,  8,  9,  1,  3,  1,  1,  2,  9,  9,  6,  1,  1,
     2,  1,  2,  9,  1,  9,  1,  1,  2,  1,  1,  2,  1,  9,  9,  9,
     9,  2,  1,  9,  1,  1,  9,  9,  9,  9,  8,  1,  2,  2,  9,  9,
     1,  9,  1,  9,  2,  3,  9,  9,  9,  9,  9,  9,  7,  9,  9,  5,
     9,  1,  2,  2,  9,  9,  1,  1,  9,  2,  1,  0,  9,  9,  1,  2,
     9,  9,  2,  9,  1,  9,  9,  9,  9,  2,  1,  2,  3,  2,  1,  1,
     1,  1,  6,  9,  9,  1,  1,  1,  9,  9,  1,  1,  1,  9,  2,  1,
     9,  9,  2,  9,  1,  9,  2,  1,  1,  1,  1,  3,  9,  1,  9,  2,
     2,  9,  1,  8,  9,  2,  9,  9,  9,  2,  9,  2,  9,  2,  2,  9,
     2,  6,  1,  9,  9,  2,  9,  1,  9,  2,  9,  5,  2,  2,  1,  9,
     9,  1,  2,  1,  2,  2,  2,  7,  7,  2,  2,  6,  2,  1,  9,  4,
     9,  2,  2,  2,  9,  9,  9,  1,  2,  1,  1,  1,  9,  9,  5,  1,
     2,  1,  9,  2,  9,  1,  4,  1,  1,  1,  9,  4,  1,  1,  2,  1,
     2,  1,  9,  2,  2,  2,  0,  1,  2,  2,  2,  2,  9,  1,  2,  9,
};

constexpr int8_t Forward[256] = {
     1, -1,  9,  0,  1,  1,  1,  9,  0,  6, -1,  9,  1,  3,  0, -1,
     9,  9,  7,  1,  1,  5,  9,  9,  1,  9,  6,  1,  9,  7,  9,  9,
     9,  0,  2,  6,  1,  9,  7,  1,  5,  0, -2,  9,  9,  1,  1,  0,
    -2,  0,  5,  9,  2,  9,  1,  4,  4,  0,  6,  5,  5,  9,  9,  9,
     9,  5,  7,  9, -1,  3,  9,  9,  9,  9,  2,  5,  2,  1,  9,  9,
     6,  9,  0,  9,  1,  1,  9,  9,  9,  9,  9,  9,  1,  9,  9,  2,
     9,  6,  2,  7,  9,  9,  3,  1,  9,  7,  4,  0,  9,  9,  0,  7,
     9,  9,  7,  9,  0,  9,  9,  9,  9,  6,  3,  6,  1,  1,  3,  0,
     6,  1,  1,  9,  9,  2,  0,  5,  9,  9, -2,  1, -1,  9,  2,  0,
     9,  9,  1,  9,  3,  9,  1,  0,  0,  4,  6,  2,  9,  2,  9,  4,
     3,  9,  2,  1,  9,  5,  9,  9,  9,  0,  9,  6,  9,  0,  3,  9,
     4,  2,  6,  9,  9,  0,  9,  5,  9,  3,  9,  1,  0,  2,  0,  9,
     9,  2,  2,  2,  0,  4,  5,  1,  2,  7,  3,  1,  5,  0,  9,  1,
     9,  1,  1,  1,  9,  9,  9,  1,  0,  2, -2,  2,  9,  9,  1,  1,
    -1,  7,  9,  3,  9,  0,  2,  4,  2, -1,  9,  1,  1,  7,  1,  0,
     0,  1,  9,  2,  2,  1,  0,  1,  0,  6,  0,  2,  9,  7,  3,  9,
};

constexpr int8_t Left[256] = {
    -1,  2,  9, -2,  0,  0,  1,  9, -4, -6,  0,  9,  1, -3, -3,  2,
     9,  9, -7,  0, -1, -5,  9,  9,  0,  9,  0,  1,  9, -7,  9,  9,
     9, -7,  2, -6,  1,  9,  7,  1, -5, -6, -1,  9,  9, -1, -1, -1,
     1, -3, -5,  9, -1,  9, -2,  0,  4, -5, -6,  5,  5,  9,  9,  9,
     9, -5,  7,  9, -1, -3,  9,  9,  9,  9,  0,  5, -1,  0,  9,  9,
     0,  9, -6,  9,  1,  0,  9,  9,  9,  9,  9,  9, -1,  9,  9,  0,
     9, -6,  0,  7,  9,  9,  3, -1,  9,  0, -4,  0,  9,  9, -5, -7,
     9,  9,  7,  9, -2,  9,  9,  9,  9,  6,  0,  0, -1,  0,  3, -1,
     6,  0,  1,  9,  9,  1, -7,  0,  9,  9, -1, -1,  1,  9,  2, -7,
     9,  9, -1,  9,  0,  9, -1,  1, -3,  0,  0,  0,  9,  0,  9,  4,
     0,  9, -2,  0,  9,  0,  9,  9,  9, -2,  9,  6,  9, -4, -3,  9,
     0,  0,  6,  9,  9, -5,  9,  0,  9, -3,  9,  0, -5,  0, -1,  9,
     9, -2, -2,  2, -1,  0,  0,  1,  0,  0,  3,  0,  5, -2,  9,  0,
     9,  1, -2,  2,  9,  9,  9,  1, -6,  2,  1,  0,  9,  9,  1,  1,
    -2,  0,  9,  0,  9, -4,  0, -4,  0, -2,  9, -1,  0, -7,  1, -4,
    -7, -1,  9,  1,  0, -1,  0,  2, -1,  0, -3, -2,  9,  0,  3,  9,
};

// Hachage de la signature d'une position → entrée de l'index (.cto).
constexpr uint32_t HashBits[64] = {
    0x3100d2bf, 0x3118e3de, 0x34ab1372, 0x2807a847,
    0x1633f566, 0x2143b359, 0x26d56488, 0x3b9e6f59,
    0x37755656, 0x3089ca7b, 0x18e92d85, 0x0cd0e9d8,
    0x1a9e3b54, 0x3eaa902f, 0x0d9bfaae, 0x2f32b45b,
    0x31ed6102, 0x3d3c8398, 0x146660e3, 0x0f8d4b76,
    0x02c77a5f, 0x146c8799, 0x1c47f51f, 0x249f8f36,
    0x24772043, 0x1fbc1e4d, 0x1e86b3fa, 0x37df36a6,
    0x16ed30e4, 0x02c3148e, 0x216e5929, 0x0636b34e,
    0x317f9f56, 0x15f09d70, 0x131026fb, 0x38c784b1,
    0x29ac3305, 0x2b485dc5, 0x3c049ddc, 0x35a9fbcd,
    0x31d5373b, 0x2b246799, 0x0a2923d3, 0x08a96e9d,
    0x30031a9f, 0x08f525b5, 0x33611c06, 0x2409db98,
    0x0ca4feb2, 0x1000b71e, 0x30566e32, 0x39447d31,
    0x194e3752, 0x08233a95, 0x0f38fe36, 0x29c7cd57,
    0x0f7b3a39, 0x328e8a16, 0x1e7d1388, 0x0fba78f5,
    0x274c7e7c, 0x1e8be65c, 0x2fa0b0bb, 0x1eb6c371,
};
