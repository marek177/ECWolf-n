#include "g_shared/a_nitemareguard.h"
#include "thingdef/thingdef.h"
#include "m_random.h"
#include "g_mapinfo.h"
#include "wl_state.h"

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
		<< n3dSpawnMarkerApplied
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
	n3dSpawnMarkerApplied = 0;
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


static FRandom pr_nitemareguard("NitemareGuardRuntime");

static AActor *NitemareFindPlayerTarget(AActor *guard)
{
	AActor *best = NULL;
	unsigned int bestDist = 0xFFFFFFFFu;

	for(AActor::Iterator it = AActor::GetIterator(); it.Next();)
	{
		AActor *candidate = it;
		if(candidate == guard || candidate->player == NULL ||
			!(candidate->flags & FL_SHOOTABLE))
			continue;

		const unsigned int dx = abs(static_cast<int>(candidate->tilex) - static_cast<int>(guard->tilex));
		const unsigned int dy = abs(static_cast<int>(candidate->tiley) - static_cast<int>(guard->tiley));
		const unsigned int dist = dx + dy;
		if(best == NULL || dist < bestDist)
		{
			best = candidate;
			bestDist = dist;
		}
	}
	return best;
}

static bool NitemareFacingPrefilter(const ANitemareGuard *guard, const AActor *player)
{
	const int dx = static_cast<int>(player->tilex) - static_cast<int>(guard->tilex);
	const int dy = static_cast<int>(player->tiley) - static_cast<int>(guard->tiley);
	const int absX = abs(dx);
	const int absY = abs(dy);
	if(absX > 8 || absY > 8)
		return false;

	if(guard->n3dTransitionControl == 0)
		return true;

	const int octant = guard->n3dOctant & 7;
	const int facingMask =
		(1 << ((octant - 1) & 7)) |
		(1 << octant) |
		(1 << ((octant + 1) & 7));

	int candidateMask = facingMask & (absX < absY ? 0x99 : 0x66);
	candidateMask &= dy >= 0 ? 0x3C : 0xC3;
	candidateMask &= dx >= 0 ? 0x0F : 0xF0;
	return candidateMask != 0;
}

static void NitemareUpdateOctant(ANitemareGuard *guard)
{
	const int x = guard->n3dMoveX;
	const int y = guard->n3dMoveY;
	if(x > 0)
		guard->n3dOctant = y < 0 ? 1 : y > 0 ? 3 : 2;
	else if(x < 0)
		guard->n3dOctant = y < 0 ? 7 : y > 0 ? 5 : 6;
	else if(y < 0)
		guard->n3dOctant = 0;
	else if(y > 0)
		guard->n3dOctant = 4;
}

static dirtype NitemareDirFromVector(signed char x, signed char y)
{
	if(x > 0)
		return y < 0 ? northeast : y > 0 ? southeast : east;
	if(x < 0)
		return y < 0 ? northwest : y > 0 ? southwest : west;
	if(y < 0)
		return north;
	if(y > 0)
		return south;
	return nodir;
}

static short NitemareScaleGuardTimer(short timer)
{
	if(gamestate.difficulty == NULL)
		return timer;

	if(gamestate.difficulty->PlayerDamageFactor > FRACUNIT)
		return timer << 1;
	if(gamestate.difficulty->PlayerDamageFactor < FRACUNIT)
		return timer >> 1;
	return timer;
}

static void NitemarePlanStrategy0(ANitemareGuard *guard, AActor *player)
{
	const fixed halfTile = TILEGLOBAL / 2;
	const int deltaX32 = halfTile != 0 ? (player->x - guard->x) / halfTile : 0;
	const int deltaY32 = halfTile != 0 ? (player->y - guard->y) / halfTile : 0;

	const int directionChoice =
		pr_nitemareguard() & (guard->n3dPerception == 0 ? 3 : 7);

	if(directionChoice == 0)
	{
		if(deltaX32 == 0) guard->n3dMoveX = 8;
		if(deltaY32 == 0) guard->n3dMoveY = 8;
	}
	else if(directionChoice == 1)
	{
		if(deltaX32 == 0) guard->n3dMoveX = -8;
		if(deltaY32 == 0) guard->n3dMoveY = -8;
	}
	else
	{
		guard->n3dMoveX = deltaX32 < 0 ? -8 : deltaX32 > 0 ? 8 : 0;
		guard->n3dMoveY = deltaY32 < 0 ? -8 : deltaY32 > 0 ? 8 : 0;
	}

	if(guard->n3dProximity != 0)
		guard->n3dTimer = 8;
	else if(guard->n3dPerception == 0)
		guard->n3dTimer = 0x18;
	else
		guard->n3dTimer = NitemareScaleGuardTimer(
			static_cast<short>(pr_nitemareguard(8) + 8));

	guard->n3dCurrentState = 0x06;
	NitemareUpdateOctant(guard);
}

void ANitemareGuard::Tick()
{
	Super::Tick();

	if(health <= 0 || n3dCurrentState == 0x0A || n3dCurrentState == 0x0B ||
		n3dCurrentState == 0x12 || n3dCurrentState == 0x15)
		return;

	// Original GUARD logic runs on the slow gameplay scheduler (~8 Hz).
	n3dSlowAccumulator += 8;
	if(n3dSlowAccumulator < 35)
		return;
	n3dSlowAccumulator -= 35;

	if(n3dStrategy != 0)
		return;

	AActor *player = NitemareFindPlayerTarget(this);
	if(player == NULL)
		return;

	target = player;

	const int dxWorld = abs(player->x - x);
	const int dyWorld = abs(player->y - y);
	n3dProximity =
		(dxWorld <= TILEGLOBAL && dyWorld <= TILEGLOBAL) ? 1 : 0;

	const bool prefilter = NitemareFacingPrefilter(this, player);
	n3dPerception = prefilter && CheckLine(player, this) ? 1 : 0;

	switch(n3dCurrentState)
	{
		case 0x00:
			if(n3dTimer > 0)
				--n3dTimer;
			if(n3dTimer == 0)
				n3dCurrentState = n3dNextState;
			break;

		case 0x02:
			// Sequence bank +0x34 is not yet wired. Preserve the recovered
			// wrapper ordering with a one-slow-tick animation placeholder.
			n3dTimer = 1;
			n3dNextState = 0x03;
			n3dCurrentState = 0x00;
			break;

		case 0x03:
		{
			const bool attackEligible =
				n3dTransitionControl == 0 ? n3dProximity != 0 : n3dPerception != 0;
			if(attackEligible)
			{
				n3dTimer = 1;
				n3dNextState = 0x04;
				n3dCurrentState = 0x00;
			}
			else
			{
				n3dCurrentState = 0x05;
			}
			break;
		}

		case 0x04:
			// Attack production is the next layer. Preserve the recovered
			// state-04 -> sequence wrapper -> state-05 ordering for now.
			n3dTimer = 1;
			n3dNextState = 0x05;
			n3dCurrentState = 0x00;
			break;

		case 0x05:
			NitemarePlanStrategy0(this, player);
			// FUN_76FC falls through to one immediate movement attempt.
			// Continue through the state-06 logic below.
			// fall through

		case 0x06:
		{
			if(dir == nodir || distance <= 0)
			{
				dir = NitemareDirFromVector(n3dMoveX, n3dMoveY);
				if(dir != nodir)
					TryWalk(this);
			}

			if(dir != nodir && distance > 0)
				MoveObj(this, TILEGLOBAL / 8);

			if(n3dTimer > 0)
				--n3dTimer;
			if(n3dTimer == 0)
				n3dCurrentState = 0x03;
			break;
		}

		case 0x07:
		case 0x08:
			if(n3dPerception != 0)
				n3dCurrentState = 0x02;
			break;

		default:
			break;
	}
}
