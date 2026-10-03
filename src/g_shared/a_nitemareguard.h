#ifndef __A_NITEMAREGUARD_H__
#define __A_NITEMAREGUARD_H__

#include "actor.h"

class ANitemareGuard : public AActor
{
	DECLARE_NATIVE_CLASS(NitemareGuard, Actor)

public:
	void Serialize(FArchive &arc);
	void Tick();

	void ConfigureRuntimeClass(int objectClass, int variant);
	bool BeginPainReaction();
	void FinishPainReaction();
	void BeginLethalTransition();
	bool AdvanceDeathSettling();
	void FinalizeDeathRuntime();

	BYTE n3dObjectClass;
	BYTE n3dStrategy;
	BYTE n3dCurrentState;
	BYTE n3dNextState;
	BYTE n3dDirectionCache;
	BYTE n3dOctant;
	BYTE n3dResultOctant;
	BYTE n3dTransitionControl;
	BYTE n3dPerceptionSucceeded;
	BYTE n3dWithinOneTile;
	BYTE n3dSpawnMarkerApplied;
	signed char n3dMoveX;
	signed char n3dMoveY;
	signed char n3dVerticalBobStep;
	short n3dTimer;
	short n3dElevation;
};

#endif
