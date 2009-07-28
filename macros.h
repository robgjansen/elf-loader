#ifndef MACROS_H
#define MACROS_H

#define EXPORT __attribute__ ((visibility("default")))
#define RETURN_ADDRESS ((unsigned long)__builtin_return_address (0))

#endif /* MACROS_H */
