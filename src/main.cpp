#include "Voxel.hpp"
#include "Debug.hpp"

int main()
{
    Debug::print("Size of class World is ", sizeof(World) / (1024 * 1024), " MB.");

    {
        Voxel * engine = new Voxel{ "Voxel Test" };
        engine->run();
        delete engine;
    }

    return 0;
}
