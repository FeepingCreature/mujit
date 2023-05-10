#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <backend.h>

int main() {
    char *result = "result is %i\n";

    Backend *backend = create_backend_x86_64();
    void *module = backend->new_module();

    Marker printf_marker = backend->declare_function(module);
    backend->import_function(module, printf_marker, (void(*)()) printf);

    Marker main_marker = backend->declare_function(module);
    Marker fact_marker = backend->declare_function(module);
    void *main_builder, *fact_builder;

    {
        void *blk0;
        void *builder = backend->new_function(module, main_marker, NULL, 0, &blk0);
        main_builder = builder;
        Reg fn_reg = backend->immediate_function(builder, fact_marker, ND);
        Reg arg = backend->immediate_int64(builder, 10, ND);
        Type arg_type = type_int64();
        RegList fact_args = { 1, &arg };
        TypeList fact_types = { 1, &arg_type };
        Reg call_ret = backend->call(builder, fn_reg, type_int64(), fact_args, fact_types, ND);
        Reg printf_reg = backend->immediate_function(builder, printf_marker, ND);
        Reg result_arg = backend->immediate_int64(builder, (int64_t) result, ND);
        Reg print_args_list[2] = {result_arg, call_ret};
        RegList print_args = { 2, print_args_list };
        Type print_types_list[2] = {type_int64(), type_int64()};
        TypeList print_types = { 2, print_types_list };
        Reg print_ret = backend->call(builder, printf_reg, type_void(), print_args, print_types, ND);
        backend->discard(builder, (RegList) { 1, &print_ret } );
        backend->ret(builder, backend->immediate_int64(builder, 0, ND), type_int64());
        // backend->ret(builder, backend->immediate_void(builder, ND), type_void());
        backend->finalize_function(builder);
    }

    {
        Type param_type[1] = {type_int64()};
        void *blk0;
        void *builder = backend->new_function(module, fact_marker, param_type, 1, &blk0);
        fact_builder = builder;
        Reg arg_reg = backend->arg(builder, 0);
        Reg one_reg = backend->immediate_int64(builder, 1, ND);
        Marker one_marker = backend->label_marker(builder);
        backend->branch_if_equal(builder, one_marker, arg_reg, one_reg);
        backend->begin_bb(builder, blk0);
        Reg pred_reg = backend->sub(builder, arg_reg, one_reg, ND);
        Type pred_type = type_int64();
        Reg fact_fun = backend->immediate_function(builder, fact_marker, ND);
        RegList recurse_args = { 1, &pred_reg };
        TypeList recurse_types = { 1, &pred_type };
        Reg call_reg = backend->call(builder, fact_fun, type_int64(), recurse_args, recurse_types, ND);
        Reg op_reg = backend->add(builder, arg_reg, call_reg, ND);
        backend->ret(builder, op_reg, type_int64());
        backend->begin_bb(builder, blk0);
        backend->label(builder, one_marker);
        backend->ret(builder, one_reg, type_int64());
        backend->finalize_function(builder);
    }

    backend->link(module);
    backend->debug_dump(fact_builder);
    backend->debug_dump(main_builder);
    int (*funcptr)() = (int(*)()) backend->get_funcptr(main_builder);
    return funcptr();
}
