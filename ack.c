#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <backend.h>

int ack_native(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ack_native(m - 1, 1);
    return ack_native(m - 1, ack_native(m, n - 1));
}

int ack_jit(int m, int n) {
    Backend *backend = create_backend_x86_64();
    void *module = backend->new_module();

    Marker ack_marker = backend->declare_function(module);
    void *ack_builder;

    {
        Type param_types[2] = {type_int64(), type_int64()};
        TypeList ack_types = {2, param_types};
        void *blk0;
        void *builder = backend->new_function(module, ack_marker, param_types, 2, &blk0);
        ack_builder = builder;
        Reg m = backend->arg(builder, 0);
        Reg n = backend->arg(builder, 1);
        Reg zero = backend->immediate_int64(builder, 0, ND);
        Reg one = backend->immediate_int64(builder, 1, ND);
        Reg m_1 = backend->sub(builder, m, one, ND);
        Reg ack_fun = backend->immediate_function(builder, ack_marker, ND);
        // if (m == 0) m_zero_marker
        Marker m_zero_marker = backend->label_marker(builder);
        backend->branch_if_equal(builder, m_zero_marker, m, zero);
        // if (n == 0) n_zero_marker
        void *blk1 = backend->begin_bb(builder, blk0);
        Marker n_zero_marker = backend->label_marker(builder);
        backend->branch_if_equal(builder, n_zero_marker, n, zero);
        // return ack(m - 1, ack(m, n - 1))
        backend->begin_bb(builder, blk1);
        Reg n_1 = backend->sub(builder, n, one, ND);
        Reg ack_inner_args_list[2] = {m, n_1};
        RegList ack_inner_args = {2, ack_inner_args_list};
        Reg ack_inner = backend->call(builder, ack_fun, type_int64(), ack_inner_args, ack_types, ND);
        Reg ack_outer_args_list[2] = {m_1, ack_inner};
        RegList ack_outer_args = {2, ack_outer_args_list};
        Reg ack_outer = backend->call(builder, ack_fun, type_int64(), ack_outer_args, ack_types, ND);
        backend->ret(builder, ack_outer, type_int64());
        // m_zero_marker: return n + 1
        backend->begin_bb(builder, blk0);
        backend->label(builder, m_zero_marker);
        Reg n1 = backend->add(builder, n, one, ND);
        backend->ret(builder, n1, type_int64());
        // n_zero_marker: return ack(m - 1, 1);
        backend->begin_bb(builder, blk1);
        backend->label(builder, n_zero_marker);
        Reg ack_args_list[2] = {m_1, one};
        RegList ack_args = {2, ack_args_list};
        Reg ack_ret = backend->call(builder, ack_fun, type_int64(), ack_args, ack_types, ND);
        backend->ret(builder, ack_ret, type_int64());

        backend->finalize_function(builder);
    }

    backend->link(module);
    // backend->debug_dump(ack_builder);
    int (*funcptr)(int, int) = (int(*)(int, int)) backend->get_funcptr(ack_builder);
    return funcptr(m, n);
}

unsigned long long millis() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}

int main(int argc, char **argv) {
    int m = atoi(argv[1]), n = atoi(argv[2]);
    unsigned long long start = millis();
    int ack_native_res = ack_native(m, n);
    unsigned long long end = millis();
    printf("ack_native(%i, %i) = %i in %llums\n", m, n, ack_native_res, end - start);
    start = millis();
    int ack_jit_res = ack_jit(m, n);
    end = millis();
    printf("ack_jit(%i, %i) = %i in %llums\n", m, n, ack_jit_res, end - start);
    return 0;
}
