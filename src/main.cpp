#include "Voxel.hpp"
#include "Debug.hpp"

void wait_on_error()
{
    int dummy = 0;
}

int main()
{
    std::set_terminate(&wait_on_error);
    Debug::print("Size of class World is ", sizeof(World) / (1024 * 1024), " MB.");

    {
        std::unique_ptr<Voxel> engine{ std::make_unique<Voxel>("Voxel Test", 5) };

        engine->run();
    }

    return 0;
}
