#include "../src/resourcefiles/nitemare_layout.h"
#include <cassert>
#include <vector>
int main()
{
    const unsigned cells = 4096;
    std::vector<unsigned char> a(cells*2), b, aw(cells*2), ao(cells*2), bw(cells*2), bo(cells*2);
    for(unsigned i=0; i<cells; ++i) { a[2*i]=i%256; a[2*i+1]=(i*17)%256; }
    b=a;
    // Synthetic 20-byte variant, deliberately unrelated to historical coordinates.
    for(unsigned i=0; i<20; ++i) b[2*(i*193)+1]^=0x80;
    NitemareLayout::ExpandCells(&a[0], &aw[0], &ao[0], cells);
    NitemareLayout::ExpandCells(&b[0], &bw[0], &bo[0], cells);
    unsigned differences=0;
    for(unsigned i=0; i<cells; ++i)
    {
        assert(aw[2*i]==a[2*i] && bw[2*i]==b[2*i]);
        assert(ao[2*i]==a[2*i+1] && bo[2*i]==b[2*i+1]);
        assert(!aw[2*i+1] && !ao[2*i+1] && !bw[2*i+1] && !bo[2*i+1]);
        differences += ao[2*i]!=bo[2*i];
    }
    assert(aw==bw && differences==20);
    // Counts and traversal are input-driven, including beyond the audited 33/39.
    for(unsigned count=0; count<=120; ++count)
    {
        std::vector<unsigned char> raw(cells*2), walls(cells*2), objects(cells*2);
        for(unsigned i=0;i<count;++i) raw[2*(cells-1-i*7)+1]=0xFE;
        NitemareLayout::ExpandCells(&raw[0],&walls[0],&objects[0],cells);
        unsigned found=0;
        for(unsigned i=0;i<cells;++i) found+=objects[2*i]==0xFE;
        assert(found==count);
    }
    unsigned long first[]={2048,2059,2070}, shifted[]={4096,4107,4118};
    assert(NitemareLayout::FindFrame(first,3,2059)==1);
    assert(NitemareLayout::FindFrame(shifted,3,4107)==1);
    assert(NitemareLayout::FindFrame(shifted,3,2059)==-1);
    assert(NitemareLayout::FindFrame(first,3,2060)==-1);
    // Reordered and shared aliases resolve by the local offsets, not ID order.
    unsigned long ids[]={2070,2048,2070,2059};
    int expected[]={2,0,2,1};
    for(unsigned i=0;i<4;++i) assert(NitemareLayout::FindFrame(first,3,ids[i])==expected[i]);
}
