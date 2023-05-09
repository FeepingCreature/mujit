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

typedef enum {
    TYPE_VOID,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_DATA,
} TypeKind;

typedef unsigned char Type_None;

typedef struct {
    int size;
} Type_Data;

typedef struct {
    TypeKind kind;
    union {
        Type_None none;
        Type_Data data;
    };
} Type;

typedef struct {
    size_t length;
    Type *ptr;
} TypeList;

static inline Type type_int64() {
    return (Type) { TYPE_INT64, {(Type_None) 0} };
}

static inline Type type_void() {
    return (Type) { TYPE_VOID, {(Type_None) 0} };
}

static inline int type_size(Type type) {
    switch (type.kind) {
        case TYPE_VOID: return 0;
        case TYPE_INT32: return 4;
        case TYPE_INT64: return 8;
        case TYPE_DATA: return type.data.size;
        default: assert(false);
    }
}

typedef struct {
    int id;
} Reg;

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
    void* (*new_function)(void *module_, Marker marker, Type* args_ptr, size_t args_num);
    // assign a funcptr to a declaration
    void (*import_function)(void *module_, Marker marker, void (*funcptr)());
    void (*finalize_function)(void *fun);
    void (*link)(void *module_);
    // Get a label marker that can later be resolved to a position in the function
    Marker (*label_marker)(void *fun);
    Reg (*immediate_void)(void *fun, RegList discards);
    Reg (*immediate_int32)(void *fun, int32_t value, RegList discards);
    Reg (*immediate_int64)(void *fun, int64_t value, RegList discards);
    Reg (*immediate_function)(void *fun, Marker marker, RegList discards);
    Reg (*call)(void *fun, Reg target, Type ret_type, RegList args, TypeList types, RegList discards);
    void (*ret)(void *fun, Reg reg, Type type);
    void (*branch)(void *fun, Marker marker);
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
