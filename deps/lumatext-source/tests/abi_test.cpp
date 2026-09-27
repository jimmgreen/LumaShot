#include <lumatext/lumatext.h>

#include <cassert>
#include <cstring>

int main() {
  static_assert(offsetof(lt_context_desc, abi_version) == 4);
  static_assert(offsetof(lt_render_config, abi_version) == 4);
  assert(lt_get_abi_version() == LT_ABI_VERSION);
  assert(std::strcmp(lt_get_version_string(), "0.1.0") == 0);
  assert(lt_context_create(nullptr, nullptr) == LT_E_INVALID_ARGUMENT);

  lt_context_desc bad{};
  bad.struct_size = sizeof(bad);
  bad.abi_version = LT_ABI_VERSION + 1;
  lt_context* context = nullptr;
  assert(lt_context_create(&bad, &context) == LT_E_INVALID_ARGUMENT);
  assert(context == nullptr);

  lt_context_desc valid{};
  valid.struct_size = offsetof(lt_context_desc, dwrite_factory) + sizeof(valid.dwrite_factory);
  valid.abi_version = LT_ABI_VERSION;
  assert(lt_context_create(&valid, &context) == LT_OK);
  assert(context != nullptr);
  lt_release(context);
  return 0;
}

