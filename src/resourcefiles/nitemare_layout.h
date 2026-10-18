#ifndef NITEMARE_LAYOUT_H
#define NITEMARE_LAYOUT_H
#include <cstddef>
namespace NitemareLayout
{
// Preserve cell order and every ID; classification/spawning belongs to xlat.
inline void ExpandCells(const unsigned char *raw, unsigned char *walls,
                        unsigned char *objects, std::size_t cells)
{
    for(std::size_t i = 0; i < cells; ++i)
    {
        walls[2*i] = raw[2*i]; walls[2*i+1] = 0;
        objects[2*i] = raw[2*i+1]; objects[2*i+1] = 0;
    }
}
template<class Positions>
inline int FindFrame(const Positions &positions, unsigned int count, unsigned long offset)
{
    for(unsigned int i = 0; i < count; ++i)
        if(positions[i] == offset) return static_cast<int>(i);
    return -1;
}
}
#endif
