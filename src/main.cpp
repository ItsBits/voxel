#include "Voxel.hpp"
#include <iostream>

int main()
{
    std::cout << "Size of class World is " << sizeof(World) / (1024 * 1024) << " MB." << std::endl;

    const char * save_data_location{ "world/" };

    {
        Voxel * engine = new Voxel{ save_data_location };
        engine->run();
        delete engine;
    }

    return 0;
}
