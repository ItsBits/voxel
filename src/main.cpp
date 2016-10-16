#include "Voxel.hpp"

int main()
{
    const char * save_data_location{ "world/" };

    {
        Voxel * engine = new Voxel{ save_data_location };
        engine->run();
        delete engine;
    }

    return 0;
}
