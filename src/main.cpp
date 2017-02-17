#include "Voxel.hpp"

#define WINDOW_NAME "Voxel Test"
#define TICK_RATE 20

int main()
{
    std::unique_ptr<Voxel>{
      std::make_unique<Voxel>(WINDOW_NAME, TICK_RATE)
    }->run();
}
