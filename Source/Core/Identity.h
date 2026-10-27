#pragma once

// Product identity. The values come from the top of CMakeLists.txt so a rename happens in one place.
namespace dust
{
inline constexpr const char* kProductName = DUST_PRODUCT_NAME;
inline constexpr const char* kCompanyName = DUST_COMPANY_NAME;
inline constexpr bool kIsFx = DUST_LOOPS_FX != 0;
} // namespace dust
