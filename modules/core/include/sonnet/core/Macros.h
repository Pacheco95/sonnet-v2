#pragma once

#define SN_NON_COPYABLE(TypeName)               \
    TypeName(const TypeName &) = delete;        \
    TypeName &operator=(const TypeName &) = delete

#define SN_NON_MOVABLE(TypeName)                \
    TypeName(TypeName &&) = delete;             \
    TypeName &operator=(TypeName &&) = delete

#define SN_UNUSED(x) (void)(x)
