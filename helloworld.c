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
    void *main_builder;

    {
        Types main_types = {0, NULL};
        X86_64_SysV main_cc = { { CALLING_CONVENTION_X86_64_SYSV }, { 0, NULL }, X86_64_CLASS_MEMORY };
        void *blk0;
        void *builder = backend->new_function(module, main_marker, main_types, &main_cc.base, &blk0);
        main_builder = builder;
        Reg printf_reg = backend->immediate_function(builder, printf_marker, ND);
        Reg helloworld_arg = backend->immediate_int64(builder, (int64_t) helloworld, ND);
        Reg printf_args_list[1] = {helloworld_arg};
        RegList printf_args = { 1, printf_args_list };
        Type printf_types_list[1] = {type(8)};
        Types printf_types = { 1, printf_types_list };
        X86_64_ArgumentClass printf_classes[1] = { X86_64_CLASS_INTEGER };
        X86_64_SysV printf_cc = { { CALLING_CONVENTION_X86_64_SYSV }, { 1, printf_classes }, X86_64_CLASS_MEMORY };
        Reg printf_ret = backend->call(builder, printf_reg, printf_args, type(0), printf_types, &printf_cc.base, ND);
        backend->discard(builder, (RegList) { 1, &printf_ret } );
        backend->ret(builder, backend->immediate_void(builder, ND), type(0), &main_cc.base);
        backend->finalize_function(builder);
    }

    backend->link(module);
    backend->debug_dump(main_builder);
    void (*funcptr)() = (void(*)()) backend->get_funcptr(main_builder);
    funcptr();
    return 0;
}
