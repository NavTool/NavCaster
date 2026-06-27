#include <iostream>

#include "app/config_loader.h"
#include "app/runtime_app.h"

int main(int argc, char **argv)
{
    const auto loaded = navcaster::caster::load_runtime_config(argc, argv);
    if (loaded.config.help) {
        std::cout << navcaster::caster::runtime_usage();
        return 0;
    }
    if (!loaded.ok) {
        std::cerr << loaded.error << "\n\n" << navcaster::caster::runtime_usage();
        return 2;
    }

    return navcaster::caster::run_runtime_app(loaded.config);
}
