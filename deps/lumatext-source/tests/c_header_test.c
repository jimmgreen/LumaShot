#include <lumatext/lumatext.h>

#include <string.h>

int main(void) {
  lt_context_desc descriptor = {0};
  lt_context* context = NULL;
  descriptor.struct_size = (uint32_t)sizeof(descriptor);
  descriptor.abi_version = LT_ABI_VERSION;
  if (lt_get_abi_version() != LT_ABI_VERSION) return 1;
  if (strcmp(lt_get_version_string(), "0.1.0") != 0) return 2;
  if (lt_context_create(&descriptor, &context) != LT_OK || context == NULL) return 3;
  lt_release(context);
  return 0;
}

