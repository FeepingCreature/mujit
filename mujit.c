#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <backend.h>

int main() {
    char *helloworld = "Hello World\n";

    Backend *backend = create_backend_x86_64();
    void *module = backend->new_module();

    Marker printf_marker = backend->declare_function(module);
    backend->import_function(module, printf_marker, (void(*)()) printf);

    Marker main_marker = backend->declare_function(module);
    Marker test_marker = backend->declare_function(module);
    void *main_builder, *test_builder;

    {
        void *builder = backend->new_function(module, main_marker, NULL, 0);
        main_builder = builder;
        Reg fn_reg = backend->immediate_function(builder, test_marker, ND);
        RegList args = { 0, NULL };
        TypeList types = { 0, NULL };
        Reg call_ret = backend->call(builder, fn_reg, type_void(), args, types, ND);
        backend->discard(builder, (RegList) { 1, &call_ret } );
        backend->ret(builder, backend->immediate_int64(builder, 5, ND), type_int64());
        // backend->ret(builder, backend->immediate_void(builder, ND), type_void());
        backend->finalize_function(builder);
    }

    {
        void *builder = backend->new_function(module, test_marker, NULL, 0);
        test_builder = builder;
        Marker body_marker = backend->label_marker(builder);
        backend->branch(builder, body_marker);
        backend->label(builder, body_marker);
        Reg param_reg = backend->immediate_int64(builder, (size_t) helloworld, ND);
        Reg fn_reg = backend->immediate_function(builder, printf_marker, ND);
        RegList args = { 1, &param_reg };
        Type argtypes = type_int64();
        TypeList types = { 1, &argtypes };
        Reg call_ret = backend->call(builder, fn_reg, type_void(), args, types, ND);
        backend->discard(builder, (RegList) { 1, &call_ret } );
        backend->ret(builder, backend->immediate_void(builder, ND), type_void());
        backend->finalize_function(builder);
    }

    backend->link(module);
    backend->debug_dump(test_builder);
    backend->debug_dump(main_builder);
    int (*funcptr)() = (int(*)()) backend->get_funcptr(main_builder);
    return funcptr();
}
