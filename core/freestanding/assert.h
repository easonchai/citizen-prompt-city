/* NDEBUG-only shim: the freestanding build always compiles with -DNDEBUG. */
#undef assert
#define assert(e) ((void)0)
