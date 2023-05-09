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

// Helper to avoid gcc -pedantic error for casting from function to data pointer.
union pedantic_convert {
    void *ptr;
    void (*funcptr)();
};

void append_x86_64_imm_w(Buffer *buffer, uint32_t imm) {
    for (int i = 0; i < 4; i++) {
        append(buffer, imm & 0xff);
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

void append_x86_64_sib(Buffer *buffer, int scalemode, int index_reg, int base_reg) {
    append(buffer, (scalemode << 6) + (index_reg << 3) + base_reg);
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

size_t append_x86_64_set_reg_marker_placeholder(Buffer *buffer, int reg) {
    append_x86_64_rex(buffer, 1, 0, 0, reg & 0x8);
    append(buffer, 0xb8 + (reg & 0x7));
    size_t offset = buffer->offset;
    append_x86_64_imm_q(buffer, 0);
    return offset;
}

void append_x86_64_add_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x01, to_reg, from_reg);
}

void append_x86_64_sub_reg_reg(Buffer *buffer, int to_reg, int from_reg) {
    append_x86_64_op_r_reg_reg(buffer, 0x29, to_reg, from_reg);
}

void append_x86_64_sub_reg_imm(Buffer *buffer, int reg, int imm) {
    append_x86_64_rex(buffer, 1, 0, 0, reg & 0x8);
    append(buffer, 0x81);
    // immediate, 81 /5
    append_x86_64_modrm(buffer, 3, 5, reg & 0x7);
    append_x86_64_imm_w(buffer, imm);
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

size_t append_x86_64_jmp_marker(Buffer *buffer) {
    append(buffer, 0xE9);
    size_t offset = buffer->offset;
    // placeholder (this will hang forever by jumping to itself)
    append_x86_64_imm_w(buffer, -5);
    return offset;
}

// reg[offset] = source
void append_x86_64_store_reg_offset(Buffer *buffer, int base_reg, int offset, int source_reg) {
    assert(offset >= 0 && offset < 128);
    append_x86_64_rex(buffer, 1, source_reg & 0x8, 0, base_reg & 0x8);
    // mov reg/mem, reg
    append(buffer, 0x89);
    int basemode = 1; // 2 for 4-byte offset
    append_x86_64_modrm(buffer, basemode, source_reg & 0x7, base_reg & 0x7);
    if (base_reg == X86_64_RSP) {
        append_x86_64_sib(buffer, 0, X86_64_RSP, X86_64_RSP);
    }
    append(buffer, (char) offset);
}

// dest = reg[offset]
void append_x86_64_load_reg_offset(Buffer *buffer, int dest_reg, int base_reg, int offset) {
    assert(offset >= 0 && offset < 128);
    append_x86_64_rex(buffer, 1, dest_reg & 0x8, 0, base_reg & 0x8);
    // mov reg, reg/mem
    append(buffer, 0x8B);
    int basemode = 1; // 2 for 4-byte offset
    append_x86_64_modrm(buffer, basemode, dest_reg & 0x7, base_reg & 0x7);
    if (base_reg == X86_64_RSP) {
        append_x86_64_sib(buffer, 0, X86_64_RSP, X86_64_RSP);
    }
    append(buffer, (char) offset);
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
    size_t length;
    Reg *ptr;
} Stackframe;

typedef struct {
    // X86_64_REG to register, -1 is unallocated
    Reg gp_regs[16];
} HwRegMap;

typedef struct {
    Marker marker;
    size_t offset;
} RelocTarget;

typedef struct {
    size_t length;
    RelocTarget *ptr;
} RelocTargets;

void alloc_reg(RegMap *map, int reg_id) {
    if (reg_id < map->length)
        return;
    map->length = reg_id + 1;
    map->ptr = realloc(map->ptr, map->length * sizeof(RegRow));
}

typedef struct {
    size_t length;
    size_t *ptr;
} Labels;

typedef struct {
    Type type;
    Reg reg;
} Arg;

typedef struct {
    size_t length;
    Arg *ptr;
} Args;

typedef struct {
    Marker declaration;
    Buffer buffer;
    Args args;
    RegMap registers;
    Stackframe stackframe;
    HwRegMap hw_reg_map;
    // label targets are resolved relatively, on finalize.
    // function targets are resolved absolutely, on link.
    RelocTargets function_targets;
    RelocTargets label_targets;
    // offset of each label in the buffer
    Labels labels;
    int next_reg;
    size_t frame_sub_offset;
    int frame_high_water_mark;
    void (*funcptr)();
} X86_64_Function_Builder;


typedef struct {
    size_t length;
    X86_64_Function_Builder **ptr;
} X86_64_Function_Builders;

typedef struct {
    Marker marker;
    int64_t value;
} X86_64_Fixed_Resolution;

typedef struct {
    size_t length;
    X86_64_Fixed_Resolution *ptr;
} X86_64_Fixed_Resolutions;

typedef struct {
    size_t next_marker;
    X86_64_Function_Builders builders;
    X86_64_Fixed_Resolutions resolutions;
} X86_64_Module;

Reg alloc_next_reg(X86_64_Function_Builder *builder) {
    int reg = builder->next_reg++;
    alloc_reg(&builder->registers, reg);
    return (Reg) { reg };
}

void set_reg_in_hwreg(X86_64_Function_Builder *builder, Reg reg, int hwreg) {
    RegRow *row = &builder->registers.ptr[reg.id];
    assert(row->type.kind == TYPE_INT64);
    assert(!IS_VALID_REG(builder->hw_reg_map.gp_regs[hwreg]));
    builder->hw_reg_map.gp_regs[hwreg] = reg;
    row->in_hw_reg = true;
    row->hw_reg = hwreg;
}

int alloc_free_stackspace_for_reg(X86_64_Function_Builder *builder, int size, Reg reg) {
    int start = 0;
    for (int i = 0; i < builder->stackframe.length; i++) {
        if (IS_VALID_REG(builder->stackframe.ptr[i])) {
            start = i + 1;
            continue;
        }
        if (i - start == size) {
            break;
        }
    }
    if (builder->stackframe.length < start + size) {
        builder->stackframe.length = start + size;
        builder->stackframe.ptr = realloc(builder->stackframe.ptr, (start + size) * sizeof(Reg));
    }
    for (int i = 0; i < size; i++) {
        builder->stackframe.ptr[start + i] = reg;
    }
    if (start + size > builder->frame_high_water_mark)
        builder->frame_high_water_mark = start + size;
    return start;
}

void spill_to_stack(X86_64_Function_Builder *builder, Reg reg) {
    RegRow *row = &builder->registers.ptr[reg.id];
    assert(row->type.kind == TYPE_INT64);
    int size = type_size(row->type);
    assert(row->in_hw_reg);
    int hwreg = row->hw_reg;
    // updates builder->stackframe
    row->stack_offset = alloc_free_stackspace_for_reg(builder, size, reg);
    row->in_hw_reg = false;
    append_x86_64_store_reg_offset(&builder->buffer, X86_64_RSP, row->stack_offset, hwreg);
    builder->hw_reg_map.gp_regs[hwreg] = INVALID_REG;
}

/**
 * Find or free up a hardware register to allocate to reg 'reg'.
 */
int alloc_hwreg(X86_64_Function_Builder *builder, Reg reg) {
    // hack: spill the reg with the smallest number
    // TODO spill the LRU reg
    Reg spill_candidate_reg = INVALID_REG;
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
        Reg current_reg = builder->hw_reg_map.gp_regs[i];
        if (!IS_VALID_REG(current_reg)) {
            builder->hw_reg_map.gp_regs[i] = reg;
            return i;
        }
        if (!IS_VALID_REG(spill_candidate_reg) || current_reg.id < spill_candidate_reg.id) {
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
        builder->hw_reg_map.gp_regs[row->hw_reg] = INVALID_REG;
    } else {
        int current_offset = row->stack_offset, size = type_size(row->type);
        assert(row->type.kind == TYPE_INT64);
        append_x86_64_load_reg_offset(&builder->buffer, hwreg, X86_64_RSP, current_offset);
        for (int i = current_offset; i < current_offset + size; i++) {
            builder->stackframe.ptr[i] = INVALID_REG;
        }
    }
    // update new location
    set_reg_in_hwreg(builder, reg, hwreg);
}

// don't update any builder stats, just copy into a known hwreg
// this is used if we want to pull a copy of a reg to use in an instr,
// but not use it going forward after.
void copy_reg_to_hw(X86_64_Function_Builder *builder, int hwreg, Reg reg) {
    RegRow *row = &builder->registers.ptr[reg.id];
    if (row->in_hw_reg) {
        if (hwreg != row->hw_reg) {
            append_x86_64_set_reg_reg(&builder->buffer, hwreg, row->hw_reg);
        }
    } else {
        int current_offset = row->stack_offset; // , size = type_size(row->type);
        assert(row->type.kind == TYPE_INT64);
        append_x86_64_load_reg_offset(&builder->buffer, hwreg, X86_64_RSP, current_offset);
    }
}

void x86_64_import_function(void *module_, Marker marker, void (*funcptr)()) {
    X86_64_Module *module = (X86_64_Module*) module_;
    X86_64_Fixed_Resolutions *resolutions = &module->resolutions;
    union pedantic_convert convert;
    convert.funcptr = funcptr;
    resolutions->ptr = realloc(resolutions->ptr, ++resolutions->length * sizeof(X86_64_Fixed_Resolution));
    resolutions->ptr[resolutions->length - 1] = (X86_64_Fixed_Resolution) {
        .marker = marker,
        .value = (int64_t) convert.ptr,
    };
}

void *x86_64_new_module() {
    X86_64_Module *module = malloc(sizeof(X86_64_Module));
    *module = (X86_64_Module) {0};
    return module;
}

void x86_64_link_module(void *module_) {
    X86_64_Module *module = (X86_64_Module*) module_;
    // Allocate the target area.
    uint64_t *marker_values = malloc(module->next_marker * sizeof(uint64_t));

    for (int i = 0; i < module->resolutions.length; i++) {
        X86_64_Fixed_Resolution *resolution = &module->resolutions.ptr[i];
        marker_values[resolution->marker.id] = resolution->value;
    }

    size_t code_length = 0;
    for (int i = 0; i < module->builders.length; i++) {
        X86_64_Function_Builder *builder = module->builders.ptr[i];
        code_length += builder->buffer.offset;
    }
    int pages = (code_length + 1023) / 1024;
    unsigned char *target = mmap(NULL, pages * 1024, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // Now that we know the target area, we can compute and resolve the offsets.
    size_t target_offset = 0;
    for (int i = 0; i < module->builders.length; i++) {
        X86_64_Function_Builder *builder = module->builders.ptr[i];
        marker_values[builder->declaration.id] = (int64_t)(target + target_offset);
        target_offset += builder->buffer.offset;
    }
    target_offset = 0;
    for (int i = 0; i < module->builders.length; i++) {
        X86_64_Function_Builder *builder = module->builders.ptr[i];
        for (int k = 0; k < builder->function_targets.length; k++) {
            RelocTarget *target = &builder->function_targets.ptr[k];
            Buffer upfixer = builder->buffer;
            upfixer.offset = target->offset;
            append_x86_64_imm_q(&upfixer, marker_values[target->marker.id]);
        }
        memcpy(target + target_offset, builder->buffer.ptr, builder->buffer.offset);
        union pedantic_convert generated_fn;
        generated_fn.ptr = target + target_offset;
        builder->funcptr = generated_fn.funcptr;
        target_offset += builder->buffer.offset;
    }
    mprotect(target, pages * 1024, PROT_EXEC);
}

Marker x86_64_label_marker(void *fun) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    Labels *labels = &builder->labels;
    labels->ptr = realloc(labels->ptr, ++labels->length * sizeof(size_t));
    // unset label
    labels->ptr[labels->length - 1] = -1;
    return (Marker) { labels->length - 1 };
}

void *x86_64_new_function(void *module_, Marker marker, Type* args_ptr, size_t args_num) {
    X86_64_Module *module = (X86_64_Module*) module_;
    X86_64_Function_Builder *builder = malloc(sizeof(X86_64_Function_Builder));
    *builder = (X86_64_Function_Builder) { 0 };
    for (int i = 0; i < 16; i++) {
        builder->hw_reg_map.gp_regs[i] = INVALID_REG;
    }
    builder->args = (Args) {
        .length = args_num,
        .ptr = malloc(args_num * sizeof(Arg)),
    };
    int arg_regs[6] = { X86_64_RDI, X86_64_RSI, X86_64_RDX, X86_64_RCX, X86_64_R8, X86_64_R9 };
    // reserve a reg for every arg
    for (int i = 0; i < args_num; i++) {
        Type arg_type = args_ptr[i];
        assert(arg_type.kind == TYPE_INT64);
        Reg reg = alloc_next_reg(builder);
        builder->args.ptr[i] = (Arg) {
            .type = arg_type,
            .reg = reg,
        };
        set_reg_in_hwreg(builder, reg, arg_regs[i]);
    }
    builder->declaration = marker;
    // header
    append_x86_64_push_reg(&builder->buffer, X86_64_RBP);
    append_x86_64_set_reg_reg(&builder->buffer, X86_64_RBP, X86_64_RSP);
    builder->frame_sub_offset = builder->buffer.offset;
    append_x86_64_sub_reg_imm(&builder->buffer, X86_64_RSP, 0);
    module->builders.ptr = realloc(module->builders.ptr, ++module->builders.length * sizeof(X86_64_Function_Builder*));
    module->builders.ptr[module->builders.length - 1] = builder;
    return builder;
}

Reg x86_64_immediate_int64(void *fun, int64_t value, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    Reg reg = alloc_next_reg(builder);
    int hwreg = alloc_hwreg(builder, reg);
    append_x86_64_set_reg_imm(&builder->buffer, hwreg, (size_t) value);
    builder->registers.ptr[reg.id] = (RegRow) {
        .in_hw_reg = true,
        .type = type_int64(),
        .hw_reg = hwreg,
    };
    return reg;
}


Reg x86_64_immediate_function(void *fun, Marker marker, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    Reg reg = alloc_next_reg(builder);
    int hwreg = alloc_hwreg(builder, reg);
    RelocTargets *targets = &builder->function_targets;
    targets->ptr = realloc(targets->ptr, ++targets->length * sizeof(RelocTarget));
    size_t offset = append_x86_64_set_reg_marker_placeholder(&builder->buffer, hwreg);
    targets->ptr[targets->length - 1] = (RelocTarget) { marker, offset };
    builder->registers.ptr[reg.id] = (RegRow) {
        .in_hw_reg = true,
        .type = type_int64(),
        .hw_reg = hwreg,
    };
    return reg;
}

Reg x86_64_arg(void *fun, int arg) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    assert(arg >= 0 && arg < builder->args.length);
    return builder->args.ptr[arg].reg;
}

Reg x86_64_immediate_void(void *fun, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    Reg reg = alloc_next_reg(builder);
    // no space necessary
    // TODO always return the same register?
    builder->registers.ptr[reg.id] = (RegRow) {
        .in_hw_reg = false,
        .type = type_void(),
    };
    return reg;
}

Reg x86_64_call(void *fun, Reg target, Type ret_type, RegList args, TypeList types, RegList discards) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    // bleh bleh bleh bleh bleh bleh
    // First spill all regs currently in hwregs to the stack.
    // TODO unless it's in discards
    for (int i = 0; i < 16; i++) {
        Reg reg = builder->hw_reg_map.gp_regs[i];
        if (IS_VALID_REG(reg)) spill_to_stack(builder, reg);
    }
    int preferred_int_regs[6] = { X86_64_RDI, X86_64_RSI, X86_64_RDX, X86_64_RCX, X86_64_R8, X86_64_R9 };
    bool occupied[16] = { 0 };
    for (int i = 0; i < args.length; i++) {
        RegRow row = builder->registers.ptr[args.ptr[i].id];
        assert(row.type.kind == TYPE_INT64);
        int hwreg = preferred_int_regs[i];
        Reg blocking_reg = builder->hw_reg_map.gp_regs[hwreg];
        if (IS_VALID_REG(blocking_reg)) spill_to_stack(builder, blocking_reg);
        copy_reg_to_hw(builder, hwreg, args.ptr[i]);
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
            copy_reg_to_hw(builder, i, target);
            break;
        }
    }
    append_x86_64_call_reg(&builder->buffer, target_hwreg);
    assert(ret_type.kind == TYPE_VOID);
    return INVALID_REG;
}

void x86_64_ret(void *fun, Reg reg, Type type) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    if (type.kind == TYPE_VOID) {
        assert(builder->registers.ptr[reg.id].type.kind == TYPE_VOID);
    } else if (type.kind == TYPE_INT64) {
        assert(builder->registers.ptr[reg.id].type.kind == TYPE_INT64);
        copy_reg_to_hw(builder, X86_64_RAX, reg);
    } else {
        assert(false);
    }
    append_x86_64_set_reg_reg(&builder->buffer, X86_64_RSP, X86_64_RBP);
    append_x86_64_pop_reg(&builder->buffer, X86_64_RBP);
    append_x86_64_ret(&builder->buffer);
}

void x86_64_branch(void *fun, Marker marker) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    assert(marker.id < builder->labels.length);
    size_t offset = append_x86_64_jmp_marker(&builder->buffer);
    RelocTargets *label_targets = &builder->label_targets;
    label_targets->ptr = realloc(label_targets->ptr, ++label_targets->length);
    label_targets->ptr[label_targets->length - 1] = (RelocTarget) {
        .marker = marker,
        .offset = offset,
    };
}

void x86_64_label(void *fun, Marker marker) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    assert(marker.id < builder->labels.length);
    builder->labels.ptr[marker.id] = builder->buffer.offset;
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

void x86_64_finalize_function(void *fun) {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    // patch stackframe allocation
    {
        // round-up to 16 to maintain x86-64 stack alignment
        int frame_size = ((builder->frame_high_water_mark + 15) / 16) * 16;
        Buffer patcher = builder->buffer;
        patcher.offset = builder->frame_sub_offset;
        append_x86_64_sub_reg_imm(&patcher, X86_64_RSP, frame_size);
    }
    // patch jump labels
    for (int i = 0; i < builder->label_targets.length; i++) {
        RelocTarget *target = &builder->label_targets.ptr[i];
        size_t label = builder->labels.ptr[target->marker.id];
        assert(label != -1);
        Buffer patcher = builder->buffer;
        patcher.offset = target->offset;
        // reloffs starts after the instr
        append_x86_64_imm_w(&patcher, label - (patcher.offset + 4));
    }
}

void (*x86_64_get_funcptr(void *fun))() {
    X86_64_Function_Builder *builder = (X86_64_Function_Builder*) fun;
    assert(builder->funcptr);
    return builder->funcptr;
}

Marker x86_64_declare_function(void *module_) {
    X86_64_Module *module = (X86_64_Module*) module_;
    return (Marker) {module->next_marker++};
}

Backend *create_backend_x86_64() {
    Backend *backend = malloc(sizeof(Backend));
    *backend = (Backend) {
        .declare_function = x86_64_declare_function,
        .new_module = x86_64_new_module,
        .new_function = x86_64_new_function,
        .import_function = x86_64_import_function,
        .immediate_void = x86_64_immediate_void,
        .immediate_int64 = x86_64_immediate_int64,
        .immediate_function = x86_64_immediate_function,
        .arg = x86_64_arg,
        .call = x86_64_call,
        .discard = x86_64_discard,
        .ret = x86_64_ret,
        .branch = x86_64_branch,
        .label = x86_64_label,
        .debug_dump = x86_64_debug_dump,
        .finalize_function = x86_64_finalize_function,
        .link = x86_64_link_module,
        .get_funcptr = x86_64_get_funcptr,
        .label_marker = x86_64_label_marker,
    };
    return backend;
}
