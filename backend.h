#ifndef __MUJIT_BACKEND_H__
#define __MUJIT_BACKEND_H__

typedef struct {
    unsigned char *ptr;
    size_t length;
    size_t offset;
} Buffer;

static inline void append(Buffer *buffer, unsigned char value) {
    if (buffer->offset == buffer->length) {
        buffer->length = buffer->length ? (buffer->length * 2) : 16;
        buffer->ptr = realloc(buffer->ptr, buffer->length);
    }
    buffer->ptr[buffer->offset] = value;
    buffer->offset += 1;
}

typedef struct {
    int size;
    // int alignment;
} Type;

static inline Type type(int size) {
    return (Type) { size };
}

typedef struct {
    size_t length;
    Type *ptr;
} Types;

typedef enum {
    CALLING_CONVENTION_X86_64_SYSV
} CallingConventionType;

typedef struct {
    CallingConventionType type;
} CallingConvention;

typedef enum {
    X86_64_CLASS_INTEGER,
    X86_64_CLASS_MEMORY,
} X86_64_ArgumentClass;

typedef struct {
    CallingConvention base;
    struct {
        size_t length;
        X86_64_ArgumentClass *ptr;
    } arguments;
    X86_64_ArgumentClass ret_class;
} X86_64_SysV;

typedef struct {
    int id;
} Reg;

#define INVALID_REG ((Reg) { -1 })
#define IS_VALID_REG(X) ((X).id != -1)

typedef struct {
    size_t length;
    Reg *ptr;
} RegList;

typedef struct {
    int32_t id;
} Marker;

typedef struct {
    void* (*new_module)();
    Marker (*declare_function)(void *module_);
    void* (*new_function)(void *module_, Marker marker, Types args, CallingConvention *cc, void **entry_bb);
    void (*finalize_function)(void *fun);
    void (*link)(void *module_);
    // Get a label marker that can later be resolved to a position in the function
    Marker (*label_marker)(void *fun);
    Reg (*immediate_void)(void *fun, RegList discards);
    Reg (*immediate_int32)(void *fun, int32_t value, RegList discards);
    // Use this for far calls (native code calls) as well!
    Reg (*immediate_int64)(void *fun, int64_t value, RegList discards);
    // Use for calling functions in the same module.
    Reg (*immediate_function)(void *fun, Marker marker, RegList discards);
    Reg (*add)(void *fun, Reg left, Reg right, RegList discards);
    Reg (*sub)(void *fun, Reg left, Reg right, RegList discards);
    Reg (*arg)(void *fun, int arg);
    Reg (*call)(void *fun, Reg target, RegList args, Type ret, Types arg_types, CallingConvention *cc, RegList discards);
    void* (*begin_bb)(void *fun, void *pred_bb);
    // These functions must be succeeded by another begin_bb call.
    void (*ret)(void *fun, Reg reg, Type type, CallingConvention *cc);
    void (*branch)(void *fun, Marker marker);
    void (*branch_if_equal)(void *fun, Marker marker, Reg first, Reg second);
    // Assign the current position to the label marker.
    void (*label)(void *fun, Marker marker);
    void (*discard)(void *fun, RegList discards);
    void (*debug_dump)(void *fun);
    void (*(*get_funcptr)(void *fun))();
    // Reg (*lt_)(Reg left, Reg right)
} Backend;

#define ND ((RegList){0, NULL})

Backend *create_backend_x86_64();

#endif
