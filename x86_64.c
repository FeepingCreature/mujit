#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <backend.h>

#define X86_64_RAX 0x0
#define X86_64_RCX 0x1
#define X86_64_RDX 0x2
#define X86_64_RBX 0x3
#define X86_64_RSP 0x4
#define X86_64_RBP 0x5
#define X86_64_RSI 0x6
#define X86_64_RDI 0x7
#define X86_64_R8  0x8
#define X86_64_R9  0x9
#define X86_64_R10 0xa
#define X86_64_R11 0xb
#define X86_64_R12 0xc
#define X86_64_R13 0xd
#define X86_64_R14 0xe
#define X86_64_R15 0xf

#define X86_64_COND_EQ 0x04
#define X86_64_COND_NE 0x05
#define X86_64_COND_LT 0x0C
#define X86_64_COND_GE 0x0D
#define X86_64_COND_LE 0x0E
#define X86_64_COND_GT 0x0F

void append_x86_64_imm_w(Buffer *buffer, uint32_t imm) {
    for (int i = 0; i < 4; i++) {
        append(buffer, 0);
        imm >>= 8;
    }
}

void append_x86_64_imm_q(Buffer *buffer, uint64_t imm) {
    for (int i = 0; i < 8; i++) {
        append(buffer, imm & 0xff);
        imm >>= 8;
    }
}

void append_x86_64_modrm(Buffer *buffer, int mod, int reg, int rm) {
    append(buffer, (mod << 6) + (reg << 3) + rm);
}

void append_x86_64_rex(Buffer *buffer, bool w, bool r, bool x, bool b) {
    append(buffer, 0x40 + (w << 3) + (r << 2) + (x << 1) + b);
}

void append_x86_64_push_reg(Buffer *buffer, int reg) {
    append(buffer, 0x50 + reg);
}

// What the docs call 'FF /r': rex, instr, modrm.
void append_x86_64_op_r_reg_reg(Buffer *buffer, unsigned char instrbyte, int to_reg, int from_reg) {
    append_x86_64_rex(buffer, 1, from_reg & 0x8, 0, to_reg & 0x8);
    append(buffer, instrbyte);
    append_x86_64_modrm(buffer, 3, from_reg & 0x7, to_reg & 0x7);
}

void append_x86_64_set_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x89, to_reg, from_reg);
}

void append_x86_64_set_reg_imm(Buffer *buffer, int reg, size_t value) {
    append_x86_64_rex(buffer, 1, 0, 0, reg & 0x8);
    append(buffer, 0xb8 + (reg & 0x7));
    append_x86_64_imm_q(buffer, value);
}

void append_x86_64_add_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x01, to_reg, from_reg);
}

void append_x86_64_sub_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x29, to_reg, from_reg);
}

void append_x86_64_cmp_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x3B, to_reg, from_reg);
}

void append_x86_64_call_reg(Buffer *buffer, int reg) {
    if (reg & 0x8) {
        append_x86_64_rex(buffer, 0, 0, 0, reg & 0x8);
    }
    append(buffer, 0xff);
    append_x86_64_modrm(buffer, 3, 2, reg & 0x7);
}

void append_x86_64_ret(Buffer *buffer) {
    append(buffer, 0xc3);
}

void append_x86_64_pop_reg(Buffer *buffer, int reg) {
    if (reg & 0x8) {
        append_x86_64_rex(buffer, 0, 0, 0, 1);
    }
    append(buffer, 0x58 + (reg & 0x7));
}

// Note: This function requires fixups, so save the offset *before* appending it.
void append_x86_64_jmp_cond(Buffer *buffer, int cond) {
    append(buffer, 0x0F);
    append(buffer, 0x80 + cond);
    // placeholder (this will hang forever by jumping to itself)
    append_x86_64_imm_w(buffer, -6);
}

typedef struct {
    bool in_hw_reg;
    Type type;
    union {
        int stack_offset;
        int hw_reg;
    };
} RegRow;

typedef struct {
    size_t length;
    RegRow *ptr;
} RegMap;

// offset to register, -1 is unallocated
typedef struct {
    int *ptr;
    size_t length;
} Stackframe;

typedef struct {
    // X86_64_REG to register, -1 is unallocated
    int gp_regs[16];
} HwRegMap;

void alloc_reg(RegMap *map, int reg_id) {
    if (reg_id < map->length)
        return;
    map->length = reg_id + 1;
    map->ptr = realloc(map->ptr, map->length * sizeof(RegRow));
}

typedef struct {
    Buffer buffer;
    RegMap registers;
    Stackframe stackframe;
    HwRegMap hw_reg_map;
    int next_reg;
} X86_64_Function_Builder;

int alloc_next_reg(X86_64_Function_Builder *builder) {
    int reg = builder->next_reg++;
    alloc_reg(&builder->registers, reg);
    return reg;
}

int alloc_free_stackspace_for_reg(X86_64_Function_Builder *builder, int size, int reg) {
    int start = 0;
    for (int i = 0; i < builder->stackframe.length; i++) {
        if (builder->stackframe.ptr[i] != -1) {
            start = i + 1;
            continue;
        }
        if (i - start == size) {
            break;
        }
    }
    if (builder->stackframe.length < start + size) {
        builder->stackframe.length = start + size;
        builder->stackframe.ptr = realloc(builder->stackframe.ptr, (start + size) * sizeof(int));
    }
    for (int i = 0; i < size; i++) {
        builder->stackframe.ptr[start + i] = reg;
    }
    return start;
}

void spill_to_stack(X86_64_Function_Builder *builder, int reg) {
    RegRow *row = &builder->registers.ptr[reg];
    int size = type_size(row->type);
    assert(row->in_hw_reg);
    builder->hw_reg_map.gp_regs[row->hw_reg] = -1;
    row->in_hw_reg = false;
    // updates builder->stackframe
    row->stack_offset = alloc_free_stackspace_for_reg(builder, size, reg);
    // TODO generate actual mov
    assert(false);
}

int alloc_hwreg(X86_64_Function_Builder *builder, int reg) {
    // hack: spill the reg with the smallest number
    // TODO spill the LRU reg
    int spill_candidate_reg = -1;
    int spill_candidate_hwreg = -1;
    bool available[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    // callee-saved, so we just don't use them
    available[X86_64_RSP] = false;
    available[X86_64_RBP] = false;
    available[X86_64_RBX] = false;
    available[X86_64_R12] = false;
    available[X86_64_R13] = false;
    available[X86_64_R14] = false;
    available[X86_64_R15] = false;
    for (int i = 0; i < 16; i++) {
        if (!available[i]) continue;
        int current_reg = builder->hw_reg_map.gp_regs[i];
        if (current_reg == -1) {
            builder->hw_reg_map.gp_regs[i] = reg;
            return i;
        }
        if (spill_candidate_reg == -1 || current_reg < spill_candidate_reg) {
            spill_candidate_reg = current_reg;
            spill_candidate_hwreg = i;
        }
    }
    spill_to_stack(builder, spill_candidate_reg);
    builder->hw_reg_map.gp_regs[spill_candidate_hwreg] = reg;
    return spill_candidate_hwreg;
}

void move_reg_to_hw(X86_64_Function_Builder *builder, Reg reg, int hwreg) {
    RegRow *row = &builder->registers.ptr[reg.id];
    // unset current location
    if (row->in_hw_reg) {
        append_x86_64_set_reg_reg(&builder->buffer, hwreg, row->hw_reg);
        builder->hw_reg_map.gp_regs[row->hw_reg] = -1;
    } else {
        int current_offset = row->stack_offset, size = type_size(row->type);
        assert(row->type.kind == TYPE_INT64);
        assert(false); // TODO
        // append_x86_64_load_reg_offset(&builder->buffer, hwreg, current_offset);
        for (int i = current_offset; i < current_offset + size; i++) {
            builder->stackframe.ptr[i] = -1;
        }
    }
    // update new location
    assert(builder->hw_reg_map.gp_regs[hwreg] == -1);
    builder->hw_reg_map.gp_regs[hwreg] = reg.id;
    row->hw_reg = hwreg;
}

void* x86_64_new_function(Type* args_ptr, size_t args_num) {
    X86_64_Function_Builder *builder = malloc(sizeof(X86_64_Function_Builder));
    *builder = (X86_64_Function_Builder) { 0 };
    for (int i = 0; i < 16; i++) {
        builder->hw_reg_map.gp_regs[i] = -1;
    }
    // header
    append_x86_64_push_reg(&builder->buffer, X86_64_RBP);
    append_x86_64_set_reg_reg(&builder->buffer, X86_64_RBP, X86_64_RSP);
    return builder;
}

Reg x86_64_immediate_int64(void *fun, int64_t value, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    int reg = alloc_next_reg(builder);
    int hwreg = alloc_hwreg(builder, reg);
    append_x86_64_set_reg_imm(&builder->buffer, hwreg, (size_t) value);
    builder->registers.ptr[reg] = (RegRow) {
        .in_hw_reg = true,
        .type = type_int64(),
        .hw_reg = hwreg,
    };
    return (Reg) { reg };
}

Reg x86_64_immediate_void(void *fun, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    int reg = alloc_next_reg(builder);
    // no space necessary
    // TODO always return the same register?
    builder->registers.ptr[reg] = (RegRow) {
        .in_hw_reg = false,
        .type = type_void(),
    };
    return (Reg) { reg };
}

Reg x86_64_call(void *fun, Reg target, Type ret_type, RegList args, TypeList types, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    // bleh bleh bleh bleh bleh bleh
    int preferred_int_regs[6] = { X86_64_RDI, X86_64_RSI, X86_64_RDX, X86_64_RCX, X86_64_R8, X86_64_R9 };
    bool occupied[16] = { 0 };
    for (int i = 0; i < args.length; i++) {
        RegRow row = builder->registers.ptr[args.ptr[i].id];
        assert(row.type.kind == TYPE_INT64);
        int hwreg = preferred_int_regs[i];
        int blocking_reg = builder->hw_reg_map.gp_regs[hwreg];
        if (blocking_reg != -1) spill_to_stack(builder, blocking_reg);
        move_reg_to_hw(builder, args.ptr[i], hwreg);
        occupied[hwreg] = true;
    }
    int target_hwreg;
    if (builder->registers.ptr[target.id].in_hw_reg) {
        target_hwreg = builder->registers.ptr[target.id].hw_reg;
    } else {
        // find a free hwreg for the funcptr
        for (int i = 0; i < 16; i++) {
            if (occupied[i]) continue;
            target_hwreg = i;
            move_reg_to_hw(builder, target, i);
            break;
        }
    }
    append_x86_64_call_reg(&builder->buffer, target_hwreg);
    assert(ret_type.kind == TYPE_VOID);
    return (Reg) { -1 };
}

void x86_64_ret(void *fun, Reg reg, Type type) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    assert(type.kind == TYPE_VOID);
    assert(builder->registers.ptr[reg.id].type.kind == TYPE_VOID);
    append_x86_64_pop_reg(&builder->buffer, X86_64_RBP);
    append_x86_64_ret(&builder->buffer);
}

void x86_64_discard(void *fun, RegList discards) {
    // TODO
}

void x86_64_debug_dump(void *fun) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    Buffer *buffer = &builder->buffer;
    printf("function generated by x86-64 backend: %i bytes\n", (int) buffer->offset);
    for (int i = 0; i < buffer->offset; i += 8) {
        for (int k = i; k < ((i + 8 < buffer->offset) ? (i + 8) : buffer->offset); k++) {
            printf("%02x ", buffer->ptr[k]);
        }
        printf("\n");
    }
}

union pedantic_convert {
    void *ptr;
    void (*funcptr)();
};

void (*x86_64_finalize_function(void *fun))() {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    int pages = (builder->buffer.offset + 1023) / 1024;
    unsigned char *target = mmap(NULL, pages * 1024, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    memcpy(target, builder->buffer.ptr, builder->buffer.offset);
    mprotect(target, builder->buffer.offset, PROT_EXEC);
    union pedantic_convert generated_fn;
    generated_fn.ptr = target;
    free(builder->buffer.ptr);
    return generated_fn.funcptr;
}

Backend *create_backend_x86_64() {
    Backend *backend = malloc(sizeof(Backend));
    *backend = (Backend) {
        .new_function = x86_64_new_function,
        .immediate_int64 = x86_64_immediate_int64,
        .immediate_void = x86_64_immediate_void,
        .call = x86_64_call,
        .discard = x86_64_discard,
        .ret = x86_64_ret,
        .debug_dump = x86_64_debug_dump,
        .finalize_function = x86_64_finalize_function,
    };
    return backend;
}
