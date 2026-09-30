/*
** file_hoverlevel.cpp
**
** Resource adapter for original Hovertank 3-D LEVELxx.HOV files.
**
** The original GPL source documents a 0xFEFE RLEW stream whose expanded
** buffer starts with a LevelDef and stores plane 0 at +32 and plane 1 at
** +32+planesize.  This adapter is an independent ECWolf implementation of
** that documented file format; no original game code is copied here.
*/

#include <cstring>

#include "m_swap.h"
#include "resourcefile.h"
#include "tarray.h"
#include "wl_def.h"
#include "zstring.h"

namespace
{

static const WORD HOVER_RLEW_TAG = 0xFEFE;
static const unsigned int HOVER_HEADER_BYTES = 32;

class FHoverPlanesLump : public FResourceLump
{
public:
	FHoverPlanesLump()
		: Width(0), Height(0)
	{
	}

	WORD Width;
	WORD Height;
	TArray<BYTE> WallPlane;
	TArray<BYTE> ObjectPlane;

protected:
	int FillCache()
	{
		const unsigned int planeBytes = static_cast<unsigned int>(Width) * Height * 2;
		const unsigned int headerBytes = 34;
		LumpSize = headerBytes + planeBytes * 3;
		Cache = new char[LumpSize];
		memset(Cache, 0, LumpSize);

		memcpy(Cache, "WDC3.1", 6);
		Cache[6] = 1; // map count, little-endian DWORD
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 10), 3);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 12), 16);

		const char *mapName = "Hovertank 3-D";
		memcpy(Cache + 14, mapName, strlen(mapName));
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 30), Width);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 32), Height);

		memcpy(Cache + headerBytes, &WallPlane[0], planeBytes);
		memcpy(Cache + headerBytes + planeBytes, &ObjectPlane[0], planeBytes);
		memset(Cache + headerBytes + planeBytes * 2, 0, planeBytes);

		RefCount = 1;
		return 1;
	}
};

class FHoverLevel : public FResourceFile
{
public:
	FHoverLevel(const char *filename, FileReader *file)
		: FResourceFile(filename, file), Marker(NULL), Planes(NULL)
	{
	}

	~FHoverLevel()
	{
		delete Marker;
		delete Planes;
	}

	FResourceLump *GetLump(int no)
	{
		if(no == 0) return Marker;
		if(no == 1) return Planes;
		return NULL;
	}

	bool Open(bool quiet)
	{
		const long fileLength = Reader->GetLength();
		if(fileLength < 8)
			return false;

		TArray<BYTE> source;
		source.Resize(fileLength);
		Reader->Seek(0, SEEK_SET);
		if(Reader->Read(&source[0], fileLength) != fileLength)
			return false;

		const DWORD expandedLength = ReadLittleLong(&source[0]);
		if(expandedLength < HOVER_HEADER_BYTES || (expandedLength & 1) != 0)
			return false;

		TArray<BYTE> expanded;
		expanded.Resize(expandedLength);

		unsigned int src = 4;
		unsigned int dst = 0;
		while(dst < expandedLength)
		{
			if(src + 2 > static_cast<unsigned int>(fileLength))
				return false;

			WORD value = ReadLittleShort(&source[src]);
			src += 2;

			if(value != HOVER_RLEW_TAG)
			{
				if(dst + 2 > expandedLength)
					return false;
				expanded[dst++] = static_cast<BYTE>(value & 0xFF);
				expanded[dst++] = static_cast<BYTE>((value >> 8) & 0xFF);
			}
			else
			{
				if(src + 4 > static_cast<unsigned int>(fileLength))
					return false;
				const WORD count = ReadLittleShort(&source[src]);
				const WORD repeated = ReadLittleShort(&source[src + 2]);
				src += 4;

				if(dst + static_cast<unsigned int>(count) * 2 > expandedLength)
					return false;
				for(unsigned int i = 0; i < count; ++i)
				{
					expanded[dst++] = static_cast<BYTE>(repeated & 0xFF);
					expanded[dst++] = static_cast<BYTE>((repeated >> 8) & 0xFF);
				}
			}
		}

		const WORD width = ReadLittleShort(&expanded[0]);
		const WORD height = ReadLittleShort(&expanded[2]);
		const WORD planes = ReadLittleShort(&expanded[4]);
		const WORD planeSize = ReadLittleShort(&expanded[14]);

		if(width == 0 || height == 0 || width > 64 || height > 64 || planes < 2)
			return false;

		const unsigned int expectedPlaneBytes =
			static_cast<unsigned int>(width) * static_cast<unsigned int>(height) * 2;
		if(planeSize < expectedPlaneBytes)
			return false;
		if(HOVER_HEADER_BYTES + static_cast<unsigned int>(planeSize) * 2 > expandedLength)
			return false;

		FString base(Filename);
		int slash = base.LastIndexOfAny("/\\:");
		if(slash >= 0)
			base = base.Mid(slash + 1);

		int level = 0;
		if(base.Len() >= 7 && base.Left(5).CompareNoCase("LEVEL") == 0)
		{
			const char a = base[5];
			const char b = base[6];
			if(a >= '0' && a <= '9' && b >= '0' && b <= '9')
				level = (a - '0') * 10 + (b - '0');
		}

		Marker = new FUncompressedLump;
		Marker->Owner = this;
		Marker->Position = 0;
		Marker->LumpSize = 0;
		FString markerName;
		markerName.Format("HOV%02d", level);
		Marker->LumpNameSetup(markerName);

		Planes = new FHoverPlanesLump;
		Planes->Owner = this;
		Planes->LumpNameSetup("PLANES");
		Planes->Width = width;
		Planes->Height = height;
		Planes->WallPlane.Resize(expectedPlaneBytes);
		Planes->ObjectPlane.Resize(expectedPlaneBytes);
		memcpy(&Planes->WallPlane[0], &expanded[HOVER_HEADER_BYTES], expectedPlaneBytes);
		memcpy(&Planes->ObjectPlane[0],
			&expanded[HOVER_HEADER_BYTES + planeSize], expectedPlaneBytes);
		Planes->LumpSize = 34 + expectedPlaneBytes * 3;

		NumLumps = 2;
		if(!quiet)
			Printf(", Hovertank level %d (%ux%u)\n", level, width, height);
		return true;
	}

private:
	FUncompressedLump *Marker;
	FHoverPlanesLump *Planes;
};

static bool IsHoverLevelName(const char *filename)
{
	FString base(filename);
	int slash = base.LastIndexOfAny("/\\:");
	if(slash >= 0)
		base = base.Mid(slash + 1);

	if(base.Len() != 11)
		return false;
	if(base.Left(5).CompareNoCase("LEVEL") != 0)
		return false;
	if(base.Mid(7).CompareNoCase(".HOV") != 0)
		return false;
	return base[5] >= '0' && base[5] <= '9' &&
		base[6] >= '0' && base[6] <= '9';
}

} // namespace

FResourceFile *CheckHoverLevel(const char *filename, FileReader *file, bool quiet)
{
	if(!IsHoverLevelName(filename))
		return NULL;

	FHoverLevel *rf = new FHoverLevel(filename, file);
	if(rf->Open(quiet))
		return rf;

	rf->Reader = NULL;
	delete rf;
	return NULL;
}
