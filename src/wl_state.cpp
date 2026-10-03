// WL_STATE.C

#include "wl_def.h"
#include "id_ca.h"
#include "id_sd.h"
#include "id_us.h"
#include "g_mapinfo.h"
#include "g_shared/a_nitemareguard.h"
#include "m_random.h"
#include "actor.h"
#include "thingdef/thingdef.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_net.h"
#include "wl_play.h"
#include "wl_state.h"
#include "templates.h"

/*
=============================================================================

							LOCAL CONSTANTS

=============================================================================
*/


/*
=============================================================================

							GLOBAL VARIABLES

=============================================================================
*/


static const dirtype opposite[9] =
	{west,southwest,south,southeast,east,northeast,north,northwest,nodir};

static const dirtype diagonal[9][9] =
{
	/* east */  {nodir,nodir,northeast,nodir,nodir,nodir,southeast,nodir,nodir},
				{nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir},
	/* north */ {northeast,nodir,nodir,nodir,northwest,nodir,nodir,nodir,nodir},
				{nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir},
	/* west */  {nodir,nodir,northwest,nodir,nodir,nodir,southwest,nodir,nodir},
				{nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir},
	/* south */ {southeast,nodir,nodir,nodir,southwest,nodir,nodir,nodir,nodir},
				{nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir},
				{nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir,nodir}
};

bool TryWalk (AActor *ob);
bool MoveObj (AActor *ob, int32_t move);

static void FirstSighting (AActor *ob, const Frame *state);

/*
=============================================================================

								LOCAL VARIABLES

=============================================================================
*/


/*
=============================================================================

						ENEMY TILE WORLD MOVEMENT CODE

=============================================================================
*/


// Determines if the MapSpot is open to receive a monster
bool TrySpot(AActor *ob, MapSpot spot)
{
	unsigned int x = spot->GetX();
	unsigned int y = spot->GetY();

	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		// We want to check where the actor is heading instead of the exact
		// tile it exists in since this is essentially how Wolf3D handled things
		// We must first determine if the monster has moved into the destination
		// tile or not.  (Half way to destination.)

		const dirtype offsetDir = iter->distance >= TILEGLOBAL/2 ? iter->dir : nodir;

		// Players need not be checked
		if(iter != ob && !iter->player && (iter->flags & FL_SOLID) &&
			static_cast<unsigned int>(iter->tilex+dirdeltax[offsetDir]) == x &&
			static_cast<unsigned int>(iter->tiley+dirdeltay[offsetDir]) == y)
			return false;
	}
	return true;
}

/*
==================================
=
= TryWalk
=
= Attempts to move ob in its current (ob->dir) direction.
=
= If blocked by either a wall or an actor returns FALSE
=
= If move is either clear or blocked only by a door, returns TRUE and sets
=
= ob->tilex         = new destination
= ob->tiley
= ob->distance      = TILEGLOBAl, or -doornumber if a door is blocking the way
=
= If a door is in the way, an OpenDoor call is made to start it opening.
= The actor code should wait until the door has been fully opened
=
==================================
*/

// Returns 1 - Wait for Door, 0 - Blocked, -1 - Continue checks
static inline short CheckSide(AActor *ob, unsigned int x, unsigned int y, MapTrigger::Side dir, bool canuse)
{
	MapSpot spot = map->GetSpot(x, y, 0);
	if(spot->tile)
	{
		if(canuse)
		{
			bool used = false;
			for(unsigned int i = 0;i < spot->triggers.Size();++i)
			{
				if(spot->triggers[i].monsterUse && spot->triggers[i].activate[dir])
				{
					if(map->ActivateTrigger(spot->triggers[i], dir, ob))
						used = true;
				}
			}
			if(used && spot->thinker)
			{
				// Wait for door
				ob->distance = -1;
				return 1;
			}
		}
		if(spot->slideAmount[dir] != 0xffff)
			return 0;
	}

	if(!TrySpot(ob, spot))
		return 0;
	return -1;
}
#define CHECKSIDE(x,y,dir) \
{ \
	short _cs; \
	if((_cs = CheckSide(ob, x, y, dir, !!(ob->flags & FL_CANUSEWALLS))) >= 0) \
		return _cs != 0; \
}
#define CHECKDIAG(x,y,dir) \
{ \
	short _cs; \
	if((_cs = CheckSide(ob, x, y, dir, false)) >= 0) \
		return _cs != 0; \
}



bool TryWalk (AActor *ob)
{
	word zonex = ob->tilex;
	word zoney = ob->tiley;

	switch (ob->dir)
	{
		case north:
			CHECKSIDE(ob->tilex,ob->tiley-1,MapTrigger::South);
			zoney--;
			break;

		case northeast:
			CHECKDIAG(ob->tilex+1,ob->tiley-1,MapTrigger::South);
			CHECKDIAG(ob->tilex+1,ob->tiley,MapTrigger::West);
			CHECKDIAG(ob->tilex,ob->tiley-1,MapTrigger::South);
			zonex++;
			zoney--;
			break;

		case east:
			CHECKSIDE(ob->tilex+1,ob->tiley,MapTrigger::West);
			zonex++;
			break;

		case southeast:
			CHECKDIAG(ob->tilex+1,ob->tiley+1,MapTrigger::North);
			CHECKDIAG(ob->tilex+1,ob->tiley,MapTrigger::West);
			CHECKDIAG(ob->tilex,ob->tiley+1,MapTrigger::North);
			zonex++;
			zoney++;
			break;

		case south:
			CHECKSIDE(ob->tilex,ob->tiley+1,MapTrigger::North);
			zoney++;
			break;

		case southwest:
			CHECKDIAG(ob->tilex-1,ob->tiley+1,MapTrigger::North);
			CHECKDIAG(ob->tilex-1,ob->tiley,MapTrigger::East);
			CHECKDIAG(ob->tilex,ob->tiley+1,MapTrigger::North);
			zonex--;
			zoney++;
			break;

		case west:
			CHECKSIDE(ob->tilex-1,ob->tiley,MapTrigger::East);
			zonex--;
			break;

		case northwest:
			CHECKDIAG(ob->tilex-1,ob->tiley-1,MapTrigger::South);
			CHECKDIAG(ob->tilex-1,ob->tiley,MapTrigger::East);
			CHECKDIAG(ob->tilex,ob->tiley-1,MapTrigger::South);
			zonex--;
			zoney--;
			break;

		case nodir:
			return false;

		default:
			Printf ("Walk: Bad dir");
			assert(ob->dir <= nodir);
	}

	ob->EnterZone(map->GetSpot(zonex, zoney, 0)->zone);

	ob->distance = TILEGLOBAL;
	return true;
}


/*
==================================
=
= SelectDodgeDir
=
= Attempts to choose and initiate a movement for ob that sends it towards
= the player while dodging
=
= If there is no possible move (ob is totally surrounded)
=
= ob->dir           =       nodir
=
= Otherwise
=
= ob->dir           = new direction to follow
= ob->distance      = TILEGLOBAL or -doornumber
= ob->tilex         = new destination
= ob->tiley
=
==================================
*/

static FRandom pr_newchasedir("NewChaseDir");
void SelectDodgeDir (AActor *ob)
{
	int         deltax,deltay,i;
	unsigned    absdx,absdy;
	dirtype     dirtry[5];
	dirtype     turnaround,tdir;

	if (ob->flags & FL_FIRSTATTACK)
	{
		//
		// turning around is only ok the very first time after noticing the
		// player
		//
		turnaround = nodir;
		ob->flags &= ~FL_FIRSTATTACK;
	}
	else
		turnaround=opposite[ob->dir];

	deltax = ob->target->tilex - ob->tilex;
	deltay = ob->target->tiley - ob->tiley;

	//
	// arange 5 direction choices in order of preference
	// the four cardinal directions plus the diagonal straight towards
	// the player
	//

	if (deltax>0)
	{
		dirtry[1]= east;
		dirtry[3]= west;
	}
	else
	{
		dirtry[1]= west;
		dirtry[3]= east;
	}

	if (deltay>0)
	{
		dirtry[2]= south;
		dirtry[4]= north;
	}
	else
	{
		dirtry[2]= north;
		dirtry[4]= south;
	}

	//
	// randomize a bit for dodging
	//
	absdx = abs(deltax);
	absdy = abs(deltay);

	if (absdx > absdy)
	{
		tdir = dirtry[1];
		dirtry[1] = dirtry[2];
		dirtry[2] = tdir;
		tdir = dirtry[3];
		dirtry[3] = dirtry[4];
		dirtry[4] = tdir;
	}

	if (pr_newchasedir() < 128)
	{
		tdir = dirtry[1];
		dirtry[1] = dirtry[2];
		dirtry[2] = tdir;
		tdir = dirtry[3];
		dirtry[3] = dirtry[4];
		dirtry[4] = tdir;
	}

	dirtry[0] = diagonal [ dirtry[1] ] [ dirtry[2] ];

	//
	// try the directions util one works
	//
	for (i=0;i<5;i++)
	{
		if ( dirtry[i] == nodir || dirtry[i] == turnaround)
			continue;

		ob->dir = dirtry[i];
		if (TryWalk(ob))
			return;
	}

	//
	// turn around only as a last resort
	//
	if (turnaround != nodir)
	{
		ob->dir = turnaround;

		if (TryWalk(ob))
			return;
	}

	ob->dir = nodir;
}


/*
============================
=
= SelectChaseDir
=
= As SelectDodgeDir, but doesn't try to dodge
=
============================
*/

void SelectChaseDir (AActor *ob)
{
	int     deltax,deltay;
	dirtype d[3];
	dirtype tdir, olddir, turnaround;


	olddir=ob->dir;
	turnaround=opposite[olddir];

	deltax=ob->target->tilex - ob->tilex;
	deltay=ob->target->tiley - ob->tiley;

	d[1]=nodir;
	d[2]=nodir;

	if (deltax>0)
		d[1]= east;
	else if (deltax<0)
		d[1]= west;
	if (deltay>0)
		d[2]=south;
	else if (deltay<0)
		d[2]=north;

	if (abs(deltay)>abs(deltax))
	{
		tdir=d[1];
		d[1]=d[2];
		d[2]=tdir;
	}

	if (d[1]==turnaround)
		d[1]=nodir;
	if (d[2]==turnaround)
		d[2]=nodir;


	if (d[1]!=nodir)
	{
		ob->dir=d[1];
		if (TryWalk(ob))
			return;     /*either moved forward or attacked*/
	}

	if (d[2]!=nodir)
	{
		ob->dir=d[2];
		if (TryWalk(ob))
			return;
	}

	/* there is no direct path to the player, so pick another direction */

	if (olddir!=nodir)
	{
		ob->dir=olddir;
		if (TryWalk(ob))
			return;
	}

	if (pr_newchasedir()>128)      /*randomly determine direction of search*/
	{
		for (tdir=north; tdir<=west; tdir=(dirtype)(tdir+1))
		{
			if (tdir!=turnaround)
			{
				ob->dir=tdir;
				if ( TryWalk(ob) )
					return;
			}
		}
	}
	else
	{
		for (tdir=west; tdir>=north; tdir=(dirtype)(tdir-1))
		{
			if (tdir!=turnaround)
			{
				ob->dir=tdir;
				if ( TryWalk(ob) )
					return;
			}
		}
	}

	if (turnaround !=  nodir)
	{
		ob->dir=turnaround;
		if (ob->dir != nodir)
		{
			if ( TryWalk(ob) )
				return;
		}
	}

	ob->dir = nodir;                // can't move
}


/*
============================
=
= SelectRunDir
=
= Run Away from player
=
============================
*/

void SelectRunDir (AActor *ob)
{
	int deltax,deltay;
	dirtype d[3];
	dirtype tdir;


	deltax=ob->target->tilex - ob->tilex;
	deltay=ob->target->tiley - ob->tiley;

	if (deltax<0)
		d[1]= east;
	else
		d[1]= west;
	if (deltay<0)
		d[2]=south;
	else
		d[2]=north;

	if (abs(deltay)>abs(deltax))
	{
		tdir=d[1];
		d[1]=d[2];
		d[2]=tdir;
	}

	ob->dir=d[1];
	if (TryWalk(ob))
		return;     /*either moved forward or attacked*/

	ob->dir=d[2];
	if (TryWalk(ob))
		return;

	/* there is no direct path to the player, so pick another direction */

	if (pr_newchasedir()>128)      /*randomly determine direction of search*/
	{
		for (tdir=north; tdir<=west; tdir=(dirtype)(tdir+1))
		{
			ob->dir=tdir;
			if ( TryWalk(ob) )
				return;
		}
	}
	else
	{
		for (tdir=west; tdir>=north; tdir=(dirtype)(tdir-1))
		{
			ob->dir=tdir;
			if ( TryWalk(ob) )
				return;
		}
	}

	ob->dir = nodir;                // can't move
}

/*
============================
=
= SelectWanderDir
=
= Pick a random direction.
=
============================
*/

void SelectWanderDir(AActor *ob)
{
	if(ob->dir == nodir)
		ob->dir = (dirtype)(pr_newchasedir()&7);

	// Randomly keep direction if possible.
	if(pr_newchasedir() < 150)
	{
		if(TryWalk(ob))
			return;
	}

	dirtype turnaround = opposite[ob->dir];
	const dirtype startdir = ob->dir;

	if (pr_newchasedir()>128)      /*randomly determine direction of search*/
	{
		for (dirtype tdir=(dirtype)((startdir+1)&7); tdir!=startdir; tdir=(dirtype)((tdir+1)&7))
		{
			if (tdir!=turnaround)
			{
				ob->dir=tdir;
				if ( TryWalk(ob) )
					return;
			}
		}
	}
	else
	{
		for (dirtype tdir=(dirtype)((startdir-1)&7); tdir!=startdir; tdir=(dirtype)((tdir-1)&7))
		{
			if (tdir!=turnaround)
			{
				ob->dir=tdir;
				if ( TryWalk(ob) )
					return;
			}
		}
	}

	if (turnaround != nodir)
	{
		ob->dir=turnaround;
		if (ob->dir != nodir)
		{
			if ( TryWalk(ob) )
				return;
		}
	}

	ob->dir = nodir;                // can't move

	
}

/*
=================
=
= MoveObj
=
= Moves ob be move global units in ob->dir direction
= Actors are not allowed to move inside the player
= Does NOT check to see if the move is tile map valid
=
= ob->x                 = adjusted for new position
= ob->y
=
=================
*/

bool MoveObj (AActor *ob, int32_t move)
{
	switch (ob->dir)
	{
		case north:
			ob->y -= move;
			break;
		case northeast:
			ob->x += move;
			ob->y -= move;
			break;
		case east:
			ob->x += move;
			break;
		case southeast:
			ob->x += move;
			ob->y += move;
			break;
		case south:
			ob->y += move;
			break;
		case southwest:
			ob->x -= move;
			ob->y += move;
			break;
		case west:
			ob->x -= move;
			break;
		case northwest:
			ob->x -= move;
			ob->y -= move;
			break;

		case nodir:
			return true;

		default:
			Printf ("MoveObj: bad dir!\n");
			assert(ob->dir <= nodir);
	}

	//
	// check to make sure it's not on top of player
	//
	for(unsigned int i = 0;i < Net::InitVars.numPlayers;++i)
	{
		if (map->CheckLink(ob->GetZone(), players[i].mo->GetZone(), true))
		{
			fixed r = ob->radius + players[i].mo->radius;
			if (abs(ob->x - players[i].mo->x) > r || abs(ob->y - players[i].mo->y) > r)
				continue;

			if ((players[i].mo->flags & FL_SHOOTABLE) && ob->GetClass()->Meta.GetMetaInt(AMETA_Damage) >= 0)
				DamageActor (players[i].mo, ob, ob->GetDamage());

			//
			// back up
			//
			switch (ob->dir)
			{
				case north:
					ob->y += move;
					break;
				case northeast:
					ob->x -= move;
					ob->y += move;
					break;
				case east:
					ob->x -= move;
					break;
				case southeast:
					ob->x -= move;
					ob->y -= move;
					break;
				case south:
					ob->y -= move;
					break;
				case southwest:
					ob->x += move;
					ob->y -= move;
					break;
				case west:
					ob->x += move;
					break;
				case northwest:
					ob->x += move;
					ob->y += move;
					break;

				case nodir:
					return false;
			}
			return false;
		}
	}
	ob->distance -=move;

	// Check for touching objects
	for(AActor::Iterator iter = AActor::GetIterator().Next();iter;)
	{
		AActor *check = iter;
		iter.Next();

		if(check == ob || (check->flags & FL_SOLID))
			continue;

		fixed r = check->radius + ob->radius;
		if(abs(ob->x - check->x) <= r &&
			abs(ob->y - check->y) <= r)
			check->Touch(ob);
	}

	return true;
}

/*
=============================================================================

								STUFF

=============================================================================
*/


/*
===================
=
= DamageActor
=
= Called when the player succesfully hits an enemy.
=
= Does damage points to enemy ob, either putting it into a stun frame or
= killing it.
=
===================
*/

static FRandom pr_nitemareguardai("NitemareGuardAI");

static AActor *NitemareGuardPlayerTarget(AActor *guard)
{
	if(guard != NULL && guard->target != NULL &&
		guard->target->player != NULL && guard->target->health > 0)
	{
		return guard->target;
	}

	for(unsigned int i = 0; i < Net::InitVars.numPlayers; ++i)
	{
		if(players[i].mo != NULL && players[i].health > 0)
			return players[i].mo;
	}
	return NULL;
}

static void NitemareGuardUpdateOctant(ANitemareGuard *guard, int moveX, int moveY)
{
	if(guard == NULL)
		return;

	if(moveX > 0)
	{
		guard->n3dOctant = moveY < 0 ? 1 : moveY > 0 ? 3 : 2;
		return;
	}
	if(moveX < 0)
	{
		guard->n3dOctant = moveY < 0 ? 7 : moveY > 0 ? 5 : 6;
		return;
	}
	if(moveY < 0)
		guard->n3dOctant = 0;
	else if(moveY > 0)
		guard->n3dOctant = 4;
}

static bool NitemareGuardPerceptionPrefilter(
	ANitemareGuard *guard, AActor *self, AActor *player, bool ignoreFacing)
{
	if(guard == NULL || self == NULL || player == NULL)
		return false;

	const int dx = static_cast<int>(player->tilex) - static_cast<int>(self->tilex);
	const int dy = static_cast<int>(player->tiley) - static_cast<int>(self->tiley);
	const int absX = abs(dx);
	const int absY = abs(dy);
	if(absX > 8 || absY > 8)
		return false;

	if(ignoreFacing)
		return true;

	const int octant = guard->n3dOctant & 7;
	int candidateMask =
		(1 << ((octant - 1) & 7)) |
		(1 << octant) |
		(1 << ((octant + 1) & 7));
	candidateMask &= absX < absY ? 0x99 : 0x66;
	candidateMask &= dy >= 0 ? 0x3C : 0xC3;
	candidateMask &= dx >= 0 ? 0x0F : 0xF0;
	return candidateMask != 0;
}

static bool NitemareGuardIntermediateActorBlocks(
	AActor *self, AActor *player, int tileX, int tileY)
{
	for(AActor::Iterator iter = AActor::GetIterator(); iter.Next();)
	{
		AActor *actor = iter;
		if(actor == self || actor == player || !(actor->flags & FL_SOLID))
			continue;
		if(static_cast<int>(actor->tilex) == tileX &&
			static_cast<int>(actor->tiley) == tileY)
		{
			return true;
		}
	}
	return false;
}

static bool NitemareGuardTracePerception(
	AActor *self, AActor *player, bool secondaryObjectChecks)
{
	if(self == NULL || player == NULL)
		return false;
	if(!CheckLine(player, self))
		return false;

	int x = self->tilex;
	int y = self->tiley;
	const int targetX = player->tilex;
	const int targetY = player->tiley;
	const int dx = abs(targetX - x);
	const int dy = abs(targetY - y);
	const int sx = x < targetX ? 1 : -1;
	const int sy = y < targetY ? 1 : -1;
	int error = dx - dy;

	for(int step = 0; step < 8; ++step)
	{
		if(x == targetX && y == targetY)
			return true;

		const int twiceError = error * 2;
		if(twiceError > -dy)
		{
			error -= dy;
			x += sx;
		}
		if(twiceError < dx)
		{
			error += dx;
			y += sy;
		}

		if(x == targetX && y == targetY)
			return true;

		if(secondaryObjectChecks &&
			NitemareGuardIntermediateActorBlocks(self, player, x, y))
		{
			return false;
		}
	}
	return false;
}

static bool NitemareGuardEvaluatePerception(
	ANitemareGuard *guard, AActor *self, AActor *player,
	bool secondaryObjectChecks, bool ignoreFacing)
{
	if(!NitemareGuardPerceptionPrefilter(guard, self, player, ignoreFacing))
		return false;
	return NitemareGuardTracePerception(self, player, secondaryObjectChecks);
}

static bool NitemareGuardEvaluateAttackGate(
	ANitemareGuard *guard, AActor *self, AActor *player)
{
	if(guard == NULL || self == NULL || player == NULL)
		return false;

	const bool perceived =
		NitemareGuardEvaluatePerception(guard, self, player, true, true);
	guard->n3dPerceptionSucceeded = perceived ? 1 : 0;

	const bool close =
		abs(player->x - self->x) <= TILEGLOBAL &&
		abs(player->y - self->y) <= TILEGLOBAL;
	guard->n3dWithinOneTile = close ? 1 : 0;

	switch(guard->n3dTransitionControl)
	{
		case 0: return close;
		case 1:
		case 2: return perceived;
		default: return false;
	}
}

static int NitemareGuardDifficultyIndex()
{
	if(gamestate.difficulty->PlayerDamageFactor > FRACUNIT)
		return 0;
	if(gamestate.difficulty->PlayerDamageFactor < FRACUNIT)
		return 2;
	return 1;
}

static signed char NitemareSignedStep(int value)
{
	return value < 0 ? -8 : value > 0 ? 8 : 0;
}

static void NitemareGuardPlanStrategy0(
	ANitemareGuard *guard, AActor *self, AActor *player)
{
	if(guard == NULL || self == NULL || player == NULL)
		return;

	const int halfTile = TILEGLOBAL / 2;
	const int deltaX32 = halfTile != 0 ? (player->x - self->x) / halfTile : 0;
	const int deltaY32 = halfTile != 0 ? (player->y - self->y) / halfTile : 0;
	const int choice =
		pr_nitemareguardai() & (guard->n3dPerceptionSucceeded == 0 ? 3 : 7);

	if(choice == 0)
	{
		if(deltaX32 == 0) guard->n3dMoveX = 8;
		if(deltaY32 == 0) guard->n3dMoveY = 8;
	}
	else if(choice == 1)
	{
		if(deltaX32 == 0) guard->n3dMoveX = -8;
		if(deltaY32 == 0) guard->n3dMoveY = -8;
	}
	else
	{
		guard->n3dMoveX = NitemareSignedStep(deltaX32);
		guard->n3dMoveY = NitemareSignedStep(deltaY32);
	}

	if(guard->n3dWithinOneTile != 0)
		guard->n3dTimer = 8;
	else if(guard->n3dPerceptionSucceeded == 0)
		guard->n3dTimer = 0x18;
	else
	{
		int timer = (pr_nitemareguardai() % 8) + 8;
		const int difficulty = NitemareGuardDifficultyIndex();
		if(difficulty == 2)
			timer >>= 1;
		else if(difficulty == 0)
			timer <<= 1;
		guard->n3dTimer = static_cast<short>(timer);
	}

	guard->n3dCurrentState = 0x06;
	NitemareGuardUpdateOctant(guard, guard->n3dMoveX, guard->n3dMoveY);
}

static bool NitemareGuardPointBlocked(
	AActor *self, AActor *player, fixed x, fixed y)
{
	if(self == NULL || map == NULL)
		return true;

	if(player != NULL)
	{
		const fixed playerExtent = (42 * TILEGLOBAL) / 64;
		if(abs(x - player->x) < playerExtent &&
			abs(y - player->y) < playerExtent)
		{
			return true;
		}
	}

	const int tileX = x >> TILESHIFT;
	const int tileY = y >> TILESHIFT;
	if(tileX < 0 || tileY < 0 ||
		tileX >= static_cast<int>(map->GetHeader().width) ||
		tileY >= static_cast<int>(map->GetHeader().height))
	{
		return true;
	}

	MapSpot spot = map->GetSpot(tileX, tileY, 0);
	if(spot == NULL)
		return true;

	if(spot->tile != NULL)
	{
		bool passableDoor = false;
		for(int side = 0; side < 4; ++side)
		{
			if(spot->slideAmount[side] == 0xffff)
			{
				passableDoor = true;
				break;
			}
		}
		if(!passableDoor)
			return true;
	}

	for(AActor::Iterator iter = AActor::GetIterator(); iter.Next();)
	{
		AActor *actor = iter;
		if(actor == self || actor == player || !(actor->flags & FL_SOLID))
			continue;
		if(static_cast<int>(actor->tilex) == tileX &&
			static_cast<int>(actor->tiley) == tileY)
		{
			return true;
		}
	}

	return false;
}

static void NitemareGuardTickVerticalBob(ANitemareGuard *guard)
{
	if(guard == NULL)
		return;
	if(guard->n3dObjectClass != 0x08 &&
		guard->n3dObjectClass != 0x14 &&
		guard->n3dObjectClass != 0x1A)
	{
		return;
	}

	if(guard->n3dVerticalBobStep == 0)
		guard->n3dVerticalBobStep = 1;

	int next = guard->n3dElevation + guard->n3dVerticalBobStep;
	if(next <= 10)
	{
		next = 10;
		guard->n3dVerticalBobStep = -guard->n3dVerticalBobStep;
	}
	else if(next >= 0x23)
	{
		next = 0x23;
		guard->n3dVerticalBobStep = -guard->n3dVerticalBobStep;
	}
	guard->n3dElevation = static_cast<short>(next);
}

static bool NitemareGuardTryMove(
	ANitemareGuard *guard, AActor *self, AActor *player)
{
	if(guard == NULL || self == NULL)
		return false;

	NitemareGuardTickVerticalBob(guard);

	const fixed unit = TILEGLOBAL / 64;
	const fixed extent = 16 * unit;
	const fixed moveX = guard->n3dMoveX * unit;
	const fixed moveY = guard->n3dMoveY * unit;
	const fixed padX = guard->n3dMoveX < 0 ? -extent :
		guard->n3dMoveX > 0 ? extent : 0;
	const fixed padY = guard->n3dMoveY < 0 ? -extent :
		guard->n3dMoveY > 0 ? extent : 0;

	bool xBlocked = false;
	bool yBlocked = false;

	if(moveX != 0)
	{
		const fixed probeX = self->x + moveX + padX;
		xBlocked =
			NitemareGuardPointBlocked(self, player, probeX, self->y - extent) ||
			NitemareGuardPointBlocked(self, player, probeX, self->y + extent);
	}
	if(moveY != 0)
	{
		const fixed probeY = self->y + moveY + padY;
		yBlocked =
			NitemareGuardPointBlocked(self, player, self->x - extent, probeY) ||
			NitemareGuardPointBlocked(self, player, self->x + extent, probeY);
	}

	fixed appliedX = xBlocked ? 0 : moveX;
	fixed appliedY = yBlocked ? 0 : moveY;
	const bool commit =
		guard->n3dCurrentState != 0x08 || (!xBlocked && !yBlocked);

	if(commit)
	{
		const unsigned int oldTileX = self->tilex;
		const unsigned int oldTileY = self->tiley;

		self->x += appliedX;
		self->y += appliedY;

		// tilex/tiley are read-only views into x/y on modern ECWolf builds.
		// Writing x/y updates them automatically.
		if(self->tilex != oldTileX || self->tiley != oldTileY)
		{
			MapSpot newSpot = map->GetSpot(self->tilex, self->tiley, 0);
			if(newSpot != NULL)
				self->EnterZone(newSpot->zone);
		}
	}

	int octantX = appliedX == 0 ? 0 : (appliedX > 0 ? 1 : -1);
	int octantY = appliedY == 0 ? 0 : (appliedY > 0 ? 1 : -1);

	if(guard->n3dCurrentState == 0x06 && xBlocked && yBlocked)
	{
		if((pr_nitemareguardai() & 1) != 0)
		{
			guard->n3dMoveX = -guard->n3dMoveX;
			octantX = guard->n3dMoveX;
			octantY = 0;
		}
		else
		{
			guard->n3dMoveY = -guard->n3dMoveY;
			octantX = 0;
			octantY = guard->n3dMoveY;
		}
	}

	if(octantX != 0 || octantY != 0)
		NitemareGuardUpdateOctant(guard, octantX, octantY);

	return commit && (appliedX != 0 || appliedY != 0);
}

static int NitemareGuardRoundedDistance(AActor *guard, AActor *player)
{
	const int dx = static_cast<int>(guard->tilex) - static_cast<int>(player->tilex);
	const int dy = static_cast<int>(guard->tiley) - static_cast<int>(player->tiley);
	const int squared = dx * dx + dy * dy;
	if(squared <= 1)
		return squared < 0 ? 0 : squared;

	int root = 0;
	while((root + 1) * (root + 1) <= squared)
		++root;
	const int remainder = squared - root * root;
	if(remainder >= root - 1)
		++root;
	return root;
}

static int NitemareGuardContactDamage(
	ANitemareGuard *guard, AActor *self, AActor *player)
{
	if(guard == NULL || self == NULL || player == NULL)
		return 0;

	const int distance = NitemareGuardRoundedDistance(self, player);
	const int seed = distance > 0 ? 100 / distance : 100;
	const int randomValue = pr_nitemareguardai();

	switch(guard->n3dObjectClass)
	{
		case 0x08: return randomValue & 0x07;
		case 0x09:
		case 0x0A: return randomValue & 0x0F;
		case 0x0B: return seed / 4;
		case 0x0C:
		case 0x1D:
		case 0x1E: return seed;
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14: return randomValue & 0x1F;
		case 0x16:
			// 0x7E52/0x51A6 gate is not yet represented in ECWolf.
			// Use the recovered non-gated branch until that global is wired.
			return 0x21;
		case 0x19: return 100;
		default: return seed / 2;
	}
}

static void NitemareGuardEnterState13(ANitemareGuard *guard)
{
	guard->n3dTimer = static_cast<short>((pr_nitemareguardai() % 0x50) + 8);
	guard->n3dCurrentState = 0x13;
	switch(guard->n3dOctant & 7)
	{
		case 0:
		case 7: guard->n3dMoveX = 0; guard->n3dMoveY = -8; break;
		case 1:
		case 2: guard->n3dMoveX = 8; guard->n3dMoveY = 0; break;
		case 3:
		case 4: guard->n3dMoveX = 0; guard->n3dMoveY = 8; break;
		default: guard->n3dMoveX = -8; guard->n3dMoveY = 0; break;
	}
}

static void NitemareGuardTickState13(
	ANitemareGuard *guard, AActor *self, AActor *player)
{
	if(guard->n3dTimer == 0)
	{
		guard->n3dStrategy = 0;
		guard->n3dCurrentState = 0x02;
		return;
	}

	--guard->n3dTimer;
	if(guard->n3dTimer < 8)
		NitemareGuardTryMove(guard, self, player);
}

static bool NitemareRemoteCannonsEnabled(AActor *player)
{
	if(player == NULL)
		return false;

	static const ClassDef *disabledClass = NULL;
	if(disabledClass == NULL)
		disabledClass = ClassDef::FindClass("NitemareRemoteCannonsDisabled");

	return disabledClass == NULL || player->FindInventory(disabledClass) == NULL;
}

ACTION_FUNCTION(A_NitemareGuardThink)
{
	if(!self->IsKindOf(NATIVE_CLASS(NitemareGuard)) || self->health <= 0)
		return true;

	ANitemareGuard *guard = static_cast<ANitemareGuard *>(self);
	AActor *player = NitemareGuardPlayerTarget(self);
	if(player == NULL)
		return true;
	self->target = player;

	// Remaining scripted state families stay separate from the generic loop.
	if(guard->n3dCurrentState == 0x0A ||
		guard->n3dCurrentState == 0x0B ||
		guard->n3dCurrentState == 0x0C ||
		guard->n3dCurrentState == 0x0D ||
		guard->n3dCurrentState == 0x11 ||
		guard->n3dCurrentState == 0x14)
	{
		return true;
	}

	switch(guard->n3dCurrentState)
	{
		case 0x01:
			if(guard->n3dTimer > 0)
				--guard->n3dTimer;
			if(guard->n3dTimer == 0)
				guard->n3dCurrentState = 0x02;
			break;

		case 0x02:
			// Sequence +0x34 and alert SFX are separate SEQDEF/SND layers.
			guard->n3dCurrentState = 0x03;
			break;

		case 0x03:
			if(NitemareGuardEvaluateAttackGate(guard, self, player))
			{
				guard->n3dCurrentState = 0x04;
			}
			else if(guard->n3dStrategy == 0)
			{
				NitemareGuardPlanStrategy0(guard, self, player);
				NitemareGuardTryMove(guard, self, player);
			}
			break;

		case 0x04:
			if(NitemareGuardEvaluateAttackGate(guard, self, player) &&
				player->player != NULL)
			{
				const int damage = NitemareGuardContactDamage(guard, self, player);
				player->player->TakeDamage(damage, self);
				if(player->health <= 0)
				{
					guard->n3dCurrentState = 0x0B;
					break;
				}
			}
			guard->n3dCurrentState = 0x05;
			break;

		case 0x05:
			if(guard->n3dStrategy == 0)
			{
				NitemareGuardPlanStrategy0(guard, self, player);
				NitemareGuardTryMove(guard, self, player);
			}
			break;

		case 0x06:
			NitemareGuardTryMove(guard, self, player);
			if(guard->n3dTimer > 0)
				--guard->n3dTimer;
			if(guard->n3dTimer == 0)
				guard->n3dCurrentState = 0x03;
			break;

		case 0x07:
		{
			const bool perceived =
				NitemareGuardEvaluatePerception(guard, self, player, false, false);
			if(perceived)
			{
				if(guard->n3dStrategy == 3)
					NitemareGuardEnterState13(guard);
				else
					guard->n3dCurrentState = 0x02;
			}
			break;
		}

		case 0x08:
			NitemareGuardTryMove(guard, self, player);
			if(guard->n3dNextState == 0x02 &&
				NitemareGuardEvaluatePerception(guard, self, player, false, false))
			{
				guard->n3dCurrentState = 0x02;
			}
			break;

		case 0x13:
			NitemareGuardTickState13(guard, self, player);
			break;

		default:
			break;
	}

	return true;
}

static FRandom pr_damagemobj("ActorTakeDamage");
static int NitemareGuardKillScore(int objectClass)
{
	static const short scores[0x1A] =
	{
		25,   75,   50,  100, 250, 150, 200, 100,
		100,   0,   150, 150, 200, -1000, 1000, 100,
		200,   0,    25, 100, 100, 250, 250, 200,
		50,    0
	};
	if(objectClass < 0x08 || objectClass > 0x21)
		return 0;
	return scores[objectClass - 0x08];
}

int NitemareGuardClassCode(AActor *ob)
{
	if(ob == NULL)
		return -1;

	if(ob->IsKindOf(NATIVE_CLASS(NitemareGuard)))
	{
		ANitemareGuard *guard = static_cast<ANitemareGuard *>(ob);
		if(guard->n3dObjectClass >= 0x08 && guard->n3dObjectClass <= 0x21)
			return guard->n3dObjectClass;
	}

	if(ob->temp1 >= 0x08 && ob->temp1 <= 0x21)
		return ob->temp1;

	for(unsigned int objectClass = 0x08; objectClass <= 0x21; ++objectClass)
	{
		FString name;
		name.Format("NitemareGuardClass%02X", objectClass);
		const ClassDef *base = ClassDef::FindClass(name.GetChars());
		if(base != NULL && ob->GetClass()->IsDescendantOf(base))
			return static_cast<int>(objectClass);
	}
	return -1;
}

int NitemareTransformGuardDamage(int rawDamage, int objectClass, int weaponSelector)
{
	if(rawDamage <= 0)
		return rawDamage;

	const bool wand = weaponSelector == 1;
	const bool silver = weaponSelector == 2;

	switch(objectClass)
	{
		case 0x0C:
		case 0x1D:
			return rawDamage >> 3;

		case 0x0D:
			return wand ? rawDamage >> 1 : rawDamage >> 3;

		case 0x0E:
		case 0x11:
		case 0x14:
			return (wand || silver) ? rawDamage >> 1 : rawDamage >> 3;

		case 0x0F:
		case 0x10:
			return wand ? rawDamage >> 1 : rawDamage >> 8;

		case 0x12:
		case 0x13:
			return rawDamage >> 2;

		case 0x15:
		case 0x19:
			return 0;

		case 0x16:
			// The original requires the independent Hamerstein gate value 3
			// (0x7E52 in the audited Win16 runtime). Do not substitute episode.
			return 0;

		case 0x17:
			return wand ? rawDamage >> 8 : rawDamage >> 2;

		case 0x18:
			if(wand) return rawDamage >> 8;
			if(silver) return rawDamage >> 4;
			return rawDamage >> 3;

		case 0x1A:
			return wand ? rawDamage >> 1 : 0;

		case 0x1B:
		case 0x1C:
			return rawDamage >> 1;

		case 0x1E:
			return wand ? 0 : rawDamage >> 3;

		case 0x1F:
			return wand ? 0 : rawDamage >> 2;

		default:
			return rawDamage;
	}
}

void NitemareDamageGuard(AActor *ob, AActor *attacker, unsigned damage)
{
	if(ob == NULL || damage == 0 || ob->health <= 0)
		return;

	const int objectClass = NitemareGuardClassCode(ob);
	if(objectClass < 0)
	{
		DamageActor(ob, attacker, damage);
		return;
	}

	int scaled = FixedMul(static_cast<int>(damage), gamestate.difficulty->PlayerDamageFactor);
	if(scaled <= 0)
		return;
	if(scaled > 255)
		scaled = 255;

	if(attacker != NULL && attacker->player)
		ob->target = attacker;

	if(scaled >= ob->health)
	{
		ANitemareGuard *guardRuntime =
			ob->IsKindOf(NATIVE_CLASS(NitemareGuard)) ?
				static_cast<ANitemareGuard *>(ob) : NULL;
		if(guardRuntime != NULL)
			guardRuntime->BeginLethalTransition();

		ob->health = 0;
		ob->flags &= ~FL_SHOOTABLE;
		if(attacker != NULL)
		{
			ob->killerx = attacker->x;
			ob->killery = attacker->y;
		}

		if(attacker != NULL && attacker->player)
			attacker->player->GivePoints(NitemareGuardKillScore(objectClass));

		const Frame *death = ob->FindState(NAME_Death);
		if(death != NULL)
			ob->SetState(death);
		else
			ob->Die();
		return;
	}

	ob->health -= scaled;

	bool enterPainState = true;
	if(ob->IsKindOf(NATIVE_CLASS(NitemareGuard)))
		enterPainState = static_cast<ANitemareGuard *>(ob)->BeginPainReaction();

	if(enterPainState && ob->PainState != NULL)
		ob->SetState(ob->PainState);
}

ACTION_FUNCTION(A_NitemareInitGuardClass)
{
	ACTION_PARAM_INT(objectClass, 0);
	ACTION_PARAM_INT(variant, 1);

	if(self->IsKindOf(NATIVE_CLASS(NitemareGuard)))
		static_cast<ANitemareGuard *>(self)->ConfigureRuntimeClass(objectClass, variant);

	self->temp1 = objectClass;
	return true;
}

ACTION_FUNCTION(A_NitemareGuardPainFinalize)
{
	if(self->IsKindOf(NATIVE_CLASS(NitemareGuard)))
		static_cast<ANitemareGuard *>(self)->FinishPainReaction();
	return true;
}

ACTION_FUNCTION(A_NitemareGuardDeathStep)
{
	if(!self->IsKindOf(NATIVE_CLASS(NitemareGuard)))
		return true;

	ANitemareGuard *guard = static_cast<ANitemareGuard *>(self);
	if(guard->AdvanceDeathSettling() && result != NULL)
	{
		// Generated DeathSettling and DeathDone frames are adjacent.
		result->JumpFrame = caller + 1;
	}
	return true;
}

ACTION_FUNCTION(A_NitemareGuardDeathFinalize)
{
	const int objectClass = NitemareGuardClassCode(self);

	if(self->IsKindOf(NATIVE_CLASS(NitemareGuard)))
		static_cast<ANitemareGuard *>(self)->FinalizeDeathRuntime();

	if(objectClass == 0x11)
	{
		// Runtime class/state now matches the original Dracula-Bat transition.
		// The visual sequence remains the existing actor sprite until SEQDEF
		// sequence 0x23 is wired into the IMG animation layer.
		self->temp1 = 0x14;
		self->health = 255;
		self->flags |= FL_SHOOTABLE | FL_SOLID;
		const Frame *guardLoop = self->FindState(FName("GuardLoop"));
		if(guardLoop != NULL)
			self->SetState(guardLoop);
		return true;
	}

	self->flags &= ~(FL_SHOOTABLE | FL_SOLID);
	return true;
}

void DamageActor (AActor *ob, AActor *attacker, unsigned damage)
{
	if (ob->player)
	{
		if ((attacker && attacker->player) && !Net::FriendlyFire())
			return;

		ob->player->TakeDamage(damage, attacker);
		return;
	}

	madenoise = true;

	//
	// do double damage if shooting a non attack mode actor
	//
	if ( !(ob->flags & FL_ATTACKMODE) )
		damage <<= 1;

	NetDPrintf("%s %d points\n", __FUNCTION__, FixedMul(damage, gamestate.difficulty->PlayerDamageFactor));
	ob->health -= FixedMul(damage, gamestate.difficulty->PlayerDamageFactor);
	// Ensure that we're targetting a player for now.
	if(attacker && attacker->player)
		ob->target = attacker;

	if (ob->health<=0)
	{
		if(attacker)
		{
			ob->killerx = attacker->x;
			ob->killery = attacker->y;
		}
		ob->Die();
	}
	else
	{
		if (! (ob->flags & FL_ATTACKMODE) )
			FirstSighting (ob, ob->SeeState);             // put into combat mode

		if(ob->PainState && pr_damagemobj() < ob->painchance)
			ob->SetState(ob->PainState);
	}
}

/*
=============================================================================

								CHECKSIGHT

=============================================================================
*/

bool CheckSlidePass(unsigned int style, unsigned int intercept, unsigned int amount)
{
	if(!amount)
		return false;

	switch(style)
	{
		default:
			return intercept < amount;
		case SLIDE_Split:
			return (unsigned int)abs((int)(FRACUNIT - intercept*2)) < amount;
		case SLIDE_Invert:
			return intercept>(FRACUNIT-amount);
	}
}

// Helps prevent leakage cases in CheckLine
static inline bool CheckAdjacentTileBlockage(int x, int y, int lastx, int lasty) {
	int adjacentX, adjacentY;
	if (abs(lastx - x) != 1 || abs(lasty - y) != 1)
		return false;

	adjacentX = lastx > x ? x + 1 : x - 1;
	adjacentY = lasty > y ? y + 1 : y - 1;

	MapSpot adjacentSpot1 = map->GetSpot(adjacentX, y, 0);
	MapSpot adjacentSpot2 = map->GetSpot(x, adjacentY, 0);
	if (adjacentSpot1->tile && adjacentSpot2->tile)
		return true;

	return false;
}

/*
=====================
=
= CheckLine
=
= Returns true if a straight line between the player and ob is unobstructed
=
=====================
*/
bool CheckLine (const AActor *ob, const AActor *ob2)
{
	int         x1,y1,xt1,yt1,x2,y2,xt2,yt2;
	int         x,y;
	int         xdist,ydist,xstep,ystep;
	int         partial,delta;
	int32_t     ltemp;
	int         xfrac,yfrac,deltafrac;
	unsigned    intercept;
	MapTile::Side	direction;
	int			lastx, lasty;

	if (!ob2)
		return false;

	x1 = ob->x >> UNSIGNEDSHIFT;            // 1/256 tile precision
	y1 = ob->y >> UNSIGNEDSHIFT;
	xt1 = x1 >> 8;
	yt1 = y1 >> 8;

	x2 = ob2->x >> UNSIGNEDSHIFT;
	y2 = ob2->y >> UNSIGNEDSHIFT;
	xt2 = ob2->tilex;
	yt2 = ob2->tiley;

	xdist = abs(xt2-xt1);

	if (xdist > 0)
	{
		if (xt2 > xt1)
		{
			partial = 256-(x1&0xff);
			xstep = 1;
			direction = MapTile::East;
		}
		else
		{
			partial = x1&0xff;
			xstep = -1;
			direction = MapTile::West;
		}

		deltafrac = abs(x2-x1);
		delta = y2-y1;
		ltemp = ((int32_t)delta<<8)/deltafrac;
		if (ltemp > 0x7fffl)
			ystep = 0x7fff;
		else if (ltemp < -0x7fffl)
			ystep = -0x7fff;
		else
			ystep = ltemp;
		yfrac = y1 + (((int32_t)ystep*partial) >>8);

		lastx = xt1;
		lasty = yt1;

		x = xt1+xstep;
		xt2 += xstep;
		do
		{
			y = yfrac>>8;
			yfrac += ystep;

			MapSpot spot = map->GetSpot(x, y, 0);
			
			if (!spot->tile)
			{
				if (CheckAdjacentTileBlockage(x, y, lastx, lasty))
					return false;
			}
			else 
			{
				if (spot->slideAmount[direction] == 0)
					return false;

				//
				// see if the door is open enough
				//
				intercept = yfrac - ystep / 2;

				if (!CheckSlidePass(spot->slideStyle, intercept, spot->slideAmount[direction]))
					return false;

			}
			lastx = x;
			lasty = y;

			x += xstep;
		} while (x != xt2);
	}

	ydist = abs(yt2-yt1);

	if (ydist > 0)
	{
		if (yt2 > yt1)
		{
			partial = 256-(y1&0xff);
			ystep = 1;
			direction = MapTile::South;
		}
		else
		{
			partial = y1&0xff;
			ystep = -1;
			direction = MapTile::North;
		}

		deltafrac = abs(y2-y1);
		delta = x2-x1;
		ltemp = ((int32_t)delta<<8)/deltafrac;
		if (ltemp > 0x7fffl)
			xstep = 0x7fff;
		else if (ltemp < -0x7fffl)
			xstep = -0x7fff;
		else
			xstep = ltemp;
		xfrac = x1 + (((int32_t)xstep*partial) >>8);

		lasty = yt1;
		lastx = xt1;

		y = yt1 + ystep;
		yt2 += ystep;
		do
		{
			x = xfrac>>8;
			xfrac += xstep;

			MapSpot spot = map->GetSpot(x, y, 0);

			if (!spot->tile)
			{
				if (CheckAdjacentTileBlockage(x, y, lastx, lasty))
					return false;
			}
			else 
			{
				if (spot->slideAmount[direction] == 0)
					return false;

				//
				// see if the door is open enough
				//
				intercept = xfrac - xstep / 2;

				if (intercept>spot->slideAmount[direction])
					return false;
			}
			lastx = x;
			lasty = y;

			y += ystep;
		} while (y != yt2);
	}

	return true;
}

/*
================
=
= CheckSight
=
= Checks a straight line between player and current object
=
= If the sight is ok, check alertness and angle to see if they notice
=
= returns true if the player has been spoted
=
================
*/

#define MINSIGHT (0x18000l*64)

static bool CheckSightTo (AActor *ob, AActor *target, double minseedist, double maxseedist, double maxheardist, double fov)
{
	if (!(target->flags & FL_SHOOTABLE))
		return false;

	bool heardnoise = madenoise;

	// Check if we can hear the player's noise
	if (heardnoise && !map->CheckLink(ob->GetZone(), target->GetZone(), true))
		heardnoise = false;

	//
	// if the target is real close, sight is automatic
	//
	int32_t deltax = target->x - ob->x;
	int32_t deltay = target->y - ob->y;
	uint32_t distance = MAX(abs(deltax), abs(deltay))*64;

	if (!(ob->flags & FL_AMBUSH) && heardnoise &&
		(maxheardist < 0.00001 ||
		distance < maxheardist))
		return true;

	if (minseedist > 0.00001 &&
		distance < minseedist)
		return false;
	if (maxseedist > 0.00001 &&
		distance > maxseedist)
		return false;

	if (distance < MINSIGHT)
		return true;

	if(fov < 359.75)
	{
		//
		// see if they are looking in the right direction
		//
		fov /= 2;
		float angle = (float) atan2 ((float) deltay, (float) deltax);
		if (angle<0)
			angle = (float) (M_PI*2+angle);
		angle_t iangle = 0-(angle_t)(angle*ANGLE_180/M_PI);
		angle_t lowerAngle = MIN(iangle, ob->angle);
		angle_t upperAngle = MAX(iangle, ob->angle);
		if(MIN(upperAngle - lowerAngle, lowerAngle - upperAngle) > angle_t(fov*ANGLE_1))
			return false;
	}

	//
	// trace a line to check for blocking tiles (corners)
	//
	return CheckLine (ob, target);
}

static int CheckSight (AActor *ob, double minseedist, double maxseedist, double maxheardist, double fov)
{
	for(unsigned int i = 0;i < Net::InitVars.numPlayers;++i)
	{
		if(CheckSightTo(ob, players[i].mo, minseedist, maxseedist, maxheardist, fov))
			return i;
	}
	return -1;
}


/*
===============
=
= FirstSighting
=
= Puts an actor into attack mode and possibly reverses the direction
= if the player is behind it
=
===============
*/

static void FirstSighting (AActor *ob, const Frame *state)
{
	PlaySoundLocActor(ob->seesound, ob);
	ob->speed = ob->runspeed;

	if (ob->distance < 0)
		ob->distance = 0;       // ignore the door opening command

	ob->flags &= ~FL_PATHING;
	ob->flags |= FL_ATTACKMODE|FL_FIRSTATTACK;

	if(state)
		ob->SetState(state);
}



/*
===============
=
= SightPlayer
=
= Called by actors that ARE NOT chasing the player.  If the player
= is detected (by sight, noise, or proximity), the actor is put into
= it's combat frame and true is returned.
=
= Incorporates a random reaction delay
=
===============
*/

static FRandom pr_sight("SightPlayer");
bool SightPlayer (AActor *ob, double minseedist, double maxseedist, double maxheardist, double fov, const Frame *state)
{
	if (notargetmode)
		return false;

	if (ob->flags & FL_ATTACKMODE)
	{
		ob->sighttime = ob->GetDefault()->sighttime;
		ob->flags &= ~FL_ATTACKMODE;
	}

	if (ob->sighttime != ob->GetDefault()->sighttime)
	{
		//
		// count down reaction time
		//
		if (ob->sightrandom)
		{
			--ob->sightrandom;
			return false;
		}

		if (ob->sighttime > 0)
		{
			--ob->sighttime;
			return false;
		}
	}
	else
	{
		int player = CheckSight (ob, minseedist, maxseedist, maxheardist, fov);
		if (player >= 0)
		{
			ob->target = players[player].mo;
			ob->flags &= ~FL_AMBUSH;

			--ob->sighttime; // We need to somehow mark we started.
			ob->sightrandom = 1; // Account for tic.
			if(ob->GetDefault()->sightrandom)
				ob->sightrandom += pr_sight()/ob->GetDefault()->sightrandom;
		}
		return false;
	}

	FirstSighting (ob, state);

	return true;
}
