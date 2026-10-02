#ifndef NITEMARE_DOOR_ADAPTER_H
#define NITEMARE_DOOR_ADAPTER_H

// Win16 1.10 only. Source: Nitemare3d-reversed @89d6c852,
// docs/REMOTE_DOORS_AND_DOS_CROSSCHECK_2026-09-22.md.
// These are original controller units, NEVER ECWolf tics or slide fractions.
namespace NitemareDoor
{
    enum State { Open = 0, Closed = 1, Opening = 2, Closing = 3, CorpseHoldOpen = 4 };
    enum { MotionUnitsPerUpdate = 2, CompletionCountdown = 32,
           ObstructionRetry = 4, OpenSound = 0x25, CloseSound = 0x26,
           SoundLatchOffset = 0x14 };

    // Deliberately no clock scale or world-unit scale is provided. The native
    // bundle also does not identify a particular original executable version.
    inline bool CanActivate(int axis)
    {
        (void)axis;
        return false;
    }

    // Discrete, evidence-backed controller events. Callers must establish the
    // original event; these helpers do not infer motion endpoints, collision,
    // timer decrement order, USE reversal or corpse detection from ECWolf.
    struct Controller
    {
        State state;
        unsigned int countdown;
        unsigned char soundLatch;

        Controller() : state(Closed), countdown(0), soundLatch(0) {}

        void CompleteMotion()
        {
            if(state == Opening || state == Closing)
            {
                state = state == Opening ? Open : Closed;
                countdown = CompletionCountdown;
            }
        }

        void RetryObstructedAutoClose()
        {
            if(state == Open && countdown == 0)
                countdown = ObstructionRetry;
        }

        void LatchActivation()
        {
            if(state != CorpseHoldOpen)
                soundLatch = 1;
        }

        // Called once when the original controller requests a motion sound,
        // NOT once per ECWolf tick. Zero means no request. Opening retains the
        // latch for later auto-close; the close path consumes it.
        unsigned int RequestMotionSound()
        {
            if(state != Opening && state != Closing)
                return 0;
            const unsigned int sound = soundLatch ?
                (state == Opening ? OpenSound : CloseSound) : 0;
            if(state == Closing)
                soundLatch = 0;
            return sound;
        }
    };
}
#endif
