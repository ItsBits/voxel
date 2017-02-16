#include "Voxel.hpp"

int main()
{
    std::unique_ptr<Voxel>{
      std::make_unique<Voxel>("Voxel Test", 20)
    }->run();
}
