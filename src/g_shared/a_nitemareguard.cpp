#include "g_shared/a_nitemareguard.h"
#include "thingdef/thingdef.h"

IMPLEMENT_CLASS(NitemareGuard)

void ANitemareGuard::Serialize(FArchive &arc)
{
	Super::Serialize(arc);
	arc << n3dObjectClass
		<< n3dStrategy
		<< n3dCurrentState
		<< n3dNextState
		<< n3dDirectionCache
		<< n3dOctant
		<< n3dResultOctant
		<< n3dTransitionControl
		<< n3dPerceptionSucceeded
		<< n3dWithinOneTile
		<< n3dMoveX
		<< n3dMoveY
		<< n3dVerticalBobStep
		<< n3dTimer
		<< n3dElevation;
}

void ANitemareGuard::ConfigureRuntimeClass(int objectClass, int variant)
{
	n3dObjectClass = static_cast<BYTE>(objectClass);
	n3dStrategy = 0;
	n3dCurrentState = 7;
	n3dNextState = 2;
	n3dDirectionCache = 8;
	n3dOctant = static_cast<BYTE>((variant & 3) * 2);
	n3dResultOctant = 8;
	n3dTransitionControl = 1;
	n3dPerceptionSucceeded = 0;
	n3dWithinOneTile = 0;
	n3dMoveX = 0;
	n3dMoveY = 0;
	n3dVerticalBobStep = 0;
	n3dTimer = 0;
	n3dElevation = 0;

	// Classes using the square one-tile proximity result instead of LOS for
	// the state-3/4 attack gate.
	if(objectClass == 0x08 || objectClass == 0x09 || objectClass == 0x0A ||
		objectClass == 0x11 || objectClass == 0x14 || objectClass == 0x1A)
	{
		n3dTransitionControl = 0;
	}

	// Gargoyles use recovered strategy 3 and the proximity decision mode.
	if(objectClass == 0x12 || objectClass == 0x13)
	{
		n3dStrategy = 3;
		n3dTransitionControl = 0;
	}

	if(objectClass == 0x15 || objectClass == 0x16)
		n3dNextState = 0;

	if(objectClass == 0x19)
	{
		n3dStrategy = 4;
		n3dCurrentState = 0x0E;
	}

	if(objectClass == 0x21)
	{
		n3dCurrentState = 0;
		n3dNextState = 0;
	}

	// Bat / Dracula-Bat / Ghost use the recovered 10..35 vertical-bob range.
	if(objectClass == 0x08 || objectClass == 0x14 || objectClass == 0x1A)
	{
		n3dElevation = 10;
		n3dVerticalBobStep = 1;
	}

	// The second four variants in an eight-way GUARD definition are moving
	// N/E/S/W starts. Their original initializer enters state 0x08.
	if((variant & 7) >= 4)
	{
		switch(variant & 3)
		{
			case 0: n3dMoveY = -8; break;
			case 1: n3dMoveX =  8; break;
			case 2: n3dMoveY =  8; break;
			case 3: n3dMoveX = -8; break;
		}
		n3dCurrentState = 0x08;
	}
}

bool ANitemareGuard::BeginPainReaction()
{
	// The original invalidates the directional sprite cache with value 8.
	n3dDirectionCache = 8;

	if(n3dStrategy == 4)
	{
		// Cannon strategy explicitly skips the normal pain transition.
		return false;
	}

	if(n3dStrategy == 2)
	{
		n3dNextState = 0x08;
		n3dCurrentState = 0x15;
		return true;
	}

	switch(n3dCurrentState)
	{
		case 0x03:
		case 0x04:
		case 0x0B:
			return false;

		case 0x07:
		case 0x08:
		case 0x15:
			n3dNextState = 0x05;
			n3dCurrentState = 0x15;
			return true;

		default:
			if(n3dCurrentState >= 0x05 && n3dCurrentState <= 0x14)
			{
				n3dNextState = n3dCurrentState;
				n3dCurrentState = 0x15;
				return true;
			}
			break;
	}

	// State 0 and other out-of-table values do not install the generic
	// state-15 wrapper in the recovered pain-routing table.
	return false;
}

void ANitemareGuard::FinishPainReaction()
{
	if(n3dCurrentState == 0x15)
		n3dCurrentState = n3dNextState;
}

void ANitemareGuard::BeginLethalTransition()
{
	n3dCurrentState = n3dElevation > 0 ? 0x12 : 0x00;
	n3dNextState = 0x09;
	// Keep a one-step timed-animation wrapper for ordinary state 0x00.
	n3dTimer = n3dCurrentState == 0x00 ? 1 : 0;
}

bool ANitemareGuard::AdvanceDeathSettling()
{
	if(n3dCurrentState == 0x00)
	{
		if(n3dTimer > 0)
		{
			--n3dTimer;
			return false;
		}
		n3dCurrentState = n3dNextState; // 0x09
		return true;
	}

	if(n3dCurrentState != 0x12)
		return true;

	if(n3dElevation > 0)
	{
		n3dElevation -= 5;
		if(n3dElevation < 0)
			n3dElevation = 0;
	}

	if(n3dTimer > 0)
		--n3dTimer;

	if(n3dElevation == 0 && n3dTimer == 0)
	{
		n3dCurrentState = n3dNextState; // fatal path stores 0x09
		return true;
	}

	return false;
}

void ANitemareGuard::FinalizeDeathRuntime()
{
	if(n3dObjectClass == 0x11)
	{
		// Dracula phase 1 -> Dracula-Bat.
		n3dObjectClass = 0x14;
		n3dCurrentState = 0x08;
		n3dNextState = 0x02;
		n3dTimer = 1;
		n3dElevation = 0x23;
		n3dDirectionCache = 8;
		health = 255;
		flags |= FL_SHOOTABLE | FL_SOLID;
		return;
	}

	n3dCurrentState = 0x0A;
}
