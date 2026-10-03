#ifndef __A_NITEMAREGUARD_H__
#define __A_NITEMAREGUARD_H__

#include "actor.h"

class ANitemareGuard : public AActor
{
	DECLARE_NATIVE_CLASS(NitemareGuard, Actor)

public:
	ANitemareGuard();

	void Serialize(FArchive &arc);

	void ConfigureRuntimeClass(int objectClass);
	bool BeginPainReaction();
	void FinishPainReaction();
	void BeginLethalTransition();
	void FinalizeDeathRuntime();

	BYTE n3dObjectClass;
	BYTE n3dStrategy;
	BYTE n3dCurrentState;
	BYTE n3dNextState;
	BYTE n3dDirectionCache;
	WORD n3dTimer;
	short n3dElevation;
};

#endif
