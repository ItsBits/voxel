#include "Voxel.hpp"
#include "Debug.hpp"

int main()
{
    Debug::print("Size of class World is ", sizeof(World) / (1024 * 1024), " MB.");

    const char * save_data_location{ "world/" };

    {
        Voxel * engine = new Voxel{ save_data_location };
        engine->run();
        delete engine;
    }

    return 0;
}
