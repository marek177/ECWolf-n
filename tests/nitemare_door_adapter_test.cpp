#include "nitemare_door_adapter.h"
#include <cassert>
using namespace NitemareDoor;
int main()
{
    assert(Open == 0 && Closed == 1 && Opening == 2 && Closing == 3 && CorpseHoldOpen == 4);
    assert(MotionUnitsPerUpdate == 2 && CompletionCountdown == 32 && ObstructionRetry == 4);
    assert(SoundLatchOffset == 0x14);
    for(int axis = -1; axis <= 3; ++axis) assert(!CanActivate(axis));
    Controller c;
    assert(c.state == Closed && c.countdown == 0 && c.soundLatch == 0);
    c.CompleteMotion(); assert(c.state == Closed && c.countdown == 0);
    c.LatchActivation(); c.state = Opening;
    assert(c.RequestMotionSound() == 0x25 && c.soundLatch == 1);
    c.CompleteMotion(); assert(c.state == Open && c.countdown == 32);
    c.RetryObstructedAutoClose(); assert(c.countdown == 32);
    c.countdown = 0; c.RetryObstructedAutoClose(); assert(c.countdown == 4);
    c.state = Closing;
    assert(c.RequestMotionSound() == 0x26 && c.soundLatch == 0);
    assert(c.RequestMotionSound() == 0);
    c.CompleteMotion(); assert(c.state == Closed && c.countdown == 32);
    c.state = CorpseHoldOpen; c.countdown = 0;
    c.LatchActivation(); c.CompleteMotion(); c.RetryObstructedAutoClose();
    assert(c.state == CorpseHoldOpen && c.countdown == 0 && c.soundLatch == 0);
    assert(c.RequestMotionSound() == 0);
}
