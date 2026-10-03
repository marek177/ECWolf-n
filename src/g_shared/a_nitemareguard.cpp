#include "g_shared/a_nitemareguard.h"
#include "thingdef/thingdef.h"

IMPLEMENT_CLASS(NitemareGuard)

ANitemareGuard::ANitemareGuard()
	: n3dObjectClass(0)
	, n3dStrategy(0)
	, n3dCurrentState(7)
	, n3dNextState(2)
	, n3dDirectionCache(0)
	, n3dTimer(0)
	, n3dElevation(0)
{
}

void ANitemareGuard::Serialize(FArchive &arc)
{
	Super::Serialize(arc);
	arc << n3dObjectClass
		<< n3dStrategy
		<< n3dCurrentState
		<< n3dNextState
		<< n3dDirectionCache
		<< n3dTimer
		<< n3dElevation;
}

void ANitemareGuard::ConfigureRuntimeClass(int objectClass)
{
	n3dObjectClass = static_cast<BYTE>(objectClass);
	n3dStrategy = 0;
	n3dCurrentState = 7;
	n3dNextState = 2;
	n3dDirectionCache = 0;
	n3dTimer = 0;
	n3dElevation = 0;

	if(objectClass == 0x19)
	{
		// Cannon: strategy 4, native state cycle starts at 0x0E.
		n3dStrategy = 4;
		n3dCurrentState = 0x0E;
	}
	else if(objectClass == 0x16)
	{
		// Dr. Hamerstein keeps generic strategy but initializes next state 0.
		n3dNextState = 0;
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
	n3dTimer = 0;
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
