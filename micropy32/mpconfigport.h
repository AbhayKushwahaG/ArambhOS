#ifndef MICROPY_INCLUDED_MPCONFIGPORT_H
#define MICROPY_INCLUDED_MPCONFIGPORT_H

#include <port/mpconfigport_common.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_PY_SYS                          (0)

#endif // MICROPY_INCLUDED_MPCONFIGPORT_H
