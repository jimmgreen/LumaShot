#include <lumatext/lumatext.h>

#include <cstring>

int main() {
  return lt_get_abi_version() == LT_ABI_VERSION &&
                 std::strcmp(lt_get_version_string(), "0.1.0") == 0
             ? 0
             : 1;
}

