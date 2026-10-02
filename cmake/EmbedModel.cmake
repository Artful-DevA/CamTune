# Turns a binary file into a C++ source defining cam::kSelfieModel.
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR bytes "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
file(WRITE "${OUT}" "// Generated from ${IN}. Do not edit.\n#include <cstddef>\nnamespace cam {\nextern const unsigned char kSelfieModel[];\nextern const size_t kSelfieModelSize;\nalignas(16) const unsigned char kSelfieModel[] = {${body}};\nconst size_t kSelfieModelSize = ${bytes};\n}\n")
