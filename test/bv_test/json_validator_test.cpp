#include <learnogl/kitchen_sink.h>
#include <learnogl/nf_simple.h>

int main(int ac, char **av)
{
    fo::memory_globals::init();
    DEFERSTAT(fo::memory_globals::shutdown());

    inistorage::Storage arg_store;
    auto error = arg_store.init_from_args(ac, av);

    if (error)
    {
        ABORT_F("%s", error.to_string());
    }

    auto ss = stringify_nfcd(arg_store.cd());

    LOG_F(INFO, "Args = %s", fo_ss::c_str(ss));
}
