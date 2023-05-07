#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <backend.h>

int main() {
    char *param = "Hello World\n";
    size_t printf_addr = (size_t) printf;

    Backend *backend = create_backend_x86_64();
    void *builder = backend->new_function(NULL, 0);
    Reg param_reg = backend->immediate_int64(builder, (size_t) param, ND);
    Reg fn_reg = backend->immediate_int64(builder, (size_t) printf_addr, ND);
    RegList args = { 1, &param_reg };
    Type argtypes = type_int64();
    TypeList types = { 1, &argtypes };
    Reg call_ret = backend->call(builder, fn_reg, type_void(), args, types, ND);
    backend->discard(builder, (RegList) { 1, &call_ret } );
    backend->ret(builder, backend->immediate_void(builder, ND), type_void());
    backend->debug_dump(builder);

    void (*funcptr)() = backend->finalize_function(builder);
    funcptr();
    return 0;
}
