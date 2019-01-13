#include <learnogl/kitchen_sink.h>
#include <learnogl/nf_simple.h>

using namespace json_schema;

int main() {
    fo::memory_globals::init();
    DEFERSTAT(fo::memory_globals::shutdown());

    const auto source_1 = R"(
    {
        nick = "rksht"
        age = 24
        high_scores = [90.70, 70.80, 91.50]
    }
    )";

    auto cd = simple_parse_cstr(source_1, true);
    DEFERSTAT(nfcd_free(cd));

    using Validator =
        StructValidator<StringValidator, SignedIntegerValidator, ArrayValidator<DoubleValidator>>;
    assert(Validator::validate(cd, nfcd_root(cd)));
}
