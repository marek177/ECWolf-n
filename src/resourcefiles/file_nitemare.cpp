/*
** file_nitemare.cpp
**
** Native resource readers for the original Nitemare 3D data files.
**
** This intentionally separates container parsing from gameplay translation.
** The formats implemented here are based on the reverse-engineering notes and
** tools in marek177/Nitemare3d-reversed, Nitemare3DDataEditor and
** nitemare3d-img.
*/

#include <cstring>

#include "m_swap.h"
#include "resourcefile.h"
#include "tarray.h"
#include "w_wad.h"
#include "wl_def.h"
#include "zstring.h"

namespace
{

static void WriteNitemareLong(BYTE *ptr, DWORD value)
{
	ptr[0] = static_cast<BYTE>(value & 0xFF);
	ptr[1] = static_cast<BYTE>((value >> 8) & 0xFF);
	ptr[2] = static_cast<BYTE>((value >> 16) & 0xFF);
	ptr[3] = static_cast<BYTE>((value >> 24) & 0xFF);
}

static FString BaseNameOf(const char *filename)
{
	FString name(filename);
	int slash = name.LastIndexOfAny("/\\:");
	if(slash >= 0)
		name = name.Mid(slash + 1);
	return name;
}

static bool IsDefinitionSpace(char c)
{
	return c == ' ' || c == '\t';
}

static bool ParseDefinitionId(const char *begin, const char *end, unsigned int &value)
{
	if(begin == end)
		return false;

	if(end - begin >= 2 && begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X'))
		begin += 2;
	if(begin == end)
		return false;

	value = 0;
	for(const char *p = begin; p != end; ++p)
	{
		unsigned int digit;
		if(*p >= '0' && *p <= '9')
			digit = static_cast<unsigned int>(*p - '0');
		else if(*p >= 'a' && *p <= 'f')
			digit = static_cast<unsigned int>(*p - 'a' + 10);
		else if(*p >= 'A' && *p <= 'F')
			digit = static_cast<unsigned int>(*p - 'A' + 10);
		else
			return false;

		value = (value << 4) | digit;
		if(value > 0xFF)
			return false;
	}
	return true;
}

static bool ValidateDefinitionTable(FileReader *reader)
{
	const long length = reader->GetLength();
	if(length <= 0)
		return false;

	char *data = new char[length + 1];
	reader->Seek(0, SEEK_SET);
	if(reader->Read(data, length) != length)
	{
		delete[] data;
		return false;
	}
	data[length] = 0;

	char *p = data;
	char *end = data + length;
	unsigned int records = 0;
	bool valid = true;

	while(p < end && valid)
	{
		char *line = p;
		while(p < end && *p != '\n')
			++p;
		char *lineEnd = p;
		if(p < end)
			++p;

		if(lineEnd > line && lineEnd[-1] == '\r')
			--lineEnd;
		while(line < lineEnd && IsDefinitionSpace(*line))
			++line;
		while(lineEnd > line && IsDefinitionSpace(lineEnd[-1]))
			--lineEnd;
		if(line == lineEnd)
			continue;

		const char *tokens[8];
		const char *scan = line;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				valid = false;
				break;
			}

			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}

		if(!valid)
			break;

		unsigned int id;
		if(!ParseDefinitionId(tokens[0], tokens[1], id))
		{
			valid = false;
			break;
		}

		// visual_code, image_name and class_name must be non-empty. The
		// description is the optional remainder of the line.
		if(tokens[2] == tokens[3] || tokens[4] == tokens[5] || tokens[6] == tokens[7])
		{
			valid = false;
			break;
		}

		++records;
	}

	delete[] data;
	return valid && records != 0;
}

class FNitemarePcmLump : public FResourceLump
{
public:
	FNitemarePcmLump() : Position(0), RawSize(0) {}

	int Position;
	int RawSize;

protected:
	int FillCache()
	{
		static const char waveHeader[44] = {
			'R','I','F','F',0,0,0,0,'W','A','V','E',
			'f','m','t',' ',16,0,0,0,1,0,1,0,
			0x11,0x2B,0,0,0x11,0x2B,0,0,1,0,8,0,
			'd','a','t','a',0,0,0,0
		};

		Cache = new char[LumpSize];
		memcpy(Cache, waveHeader, sizeof(waveHeader));
		WriteNitemareLong(reinterpret_cast<BYTE *>(Cache + 4), RawSize + 36);
		WriteNitemareLong(reinterpret_cast<BYTE *>(Cache + 24), 11025);
		WriteNitemareLong(reinterpret_cast<BYTE *>(Cache + 28), 11025);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 32), 1);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 34), 8);
		WriteNitemareLong(reinterpret_cast<BYTE *>(Cache + 40), RawSize);

		Owner->Reader->Seek(Position, SEEK_SET);
		Owner->Reader->Read(Cache + 44, RawSize);
		RefCount = 1;
		return 1;
	}
};

class FNitemareMapLump : public FResourceLump
{
public:
	FNitemareMapLump() : Position(0), Episode(0), Level(0) {}

	int Position;
	int Episode;
	int Level;

protected:
	int FillCache()
	{
		static const int Width = 64;
		static const int Height = 64;
		static const int Cells = Width * Height;
		static const int RawBytes = Cells * 2;
		static const int HeaderBytes = 34;
		static const int Planes = 3;
		static const int PlaneBytes = Cells * 2;

		BYTE raw[RawBytes];
		Owner->Reader->Seek(Position, SEEK_SET);
		if(Owner->Reader->Read(raw, RawBytes) != RawBytes)
			return 0;

		Cache = new char[LumpSize];
		memset(Cache, 0, LumpSize);
		memcpy(Cache, "WDC3.1", 6);
		WriteNitemareLong(reinterpret_cast<BYTE *>(Cache + 6), 1);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 10), Planes);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 12), 16);

		char mapName[16];
		memset(mapName, 0, sizeof(mapName));
		mysnprintf(mapName, sizeof(mapName), "Nitemare E%dM%d", Episode, Level);
		memcpy(Cache + 14, mapName, sizeof(mapName));
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 30), Width);
		WriteLittleShort(reinterpret_cast<BYTE *>(Cache + 32), Height);

		BYTE *wallPlane = reinterpret_cast<BYTE *>(Cache + HeaderBytes);
		BYTE *objectPlane = wallPlane + PlaneBytes;
		BYTE *thirdPlane = objectPlane + PlaneBytes;
		for(int i = 0; i < Cells; ++i)
		{
			WriteLittleShort(wallPlane + i * 2, raw[i * 2]);
			WriteLittleShort(objectPlane + i * 2, raw[i * 2 + 1]);
		}
		memset(thirdPlane, 0, PlaneBytes);

		RefCount = 1;
		return 1;
	}
};

class FNitemareResourceFile : public FResourceFile
{
public:
	FNitemareResourceFile(const char *filename, FileReader *file)
		: FResourceFile(filename, file)
	{
	}

	~FNitemareResourceFile()
	{
		for(unsigned int i = 0; i < Lumps.Size(); ++i)
			delete Lumps[i];
	}

	FResourceLump *GetLump(int no)
	{
		return no >= 0 && static_cast<unsigned int>(no) < Lumps.Size() ? Lumps[no] : NULL;
	}

	bool Open(bool quiet)
	{
		const FString base = BaseNameOf(Filename);
		bool ok = false;

		if(base.CompareNoCase("IMG.1") == 0) ok = OpenImg(1);
		else if(base.CompareNoCase("IMG.2") == 0) ok = OpenImg(2);
		else if(base.CompareNoCase("IMG.3") == 0) ok = OpenImg(3);
		else if(base.CompareNoCase("MAP.1") == 0) ok = OpenMap(1);
		else if(base.CompareNoCase("MAP.2") == 0) ok = OpenMap(2);
		else if(base.CompareNoCase("MAP.3") == 0) ok = OpenMap(3);
		else if(base.CompareNoCase("WALLS.1") == 0) ok = OpenDefinitions(1, true);
		else if(base.CompareNoCase("WALLS.2") == 0) ok = OpenDefinitions(2, true);
		else if(base.CompareNoCase("WALLS.3") == 0) ok = OpenDefinitions(3, true);
		else if(base.CompareNoCase("OBJECTS.1") == 0) ok = OpenDefinitions(1, false);
		else if(base.CompareNoCase("OBJECTS.2") == 0) ok = OpenDefinitions(2, false);
		else if(base.CompareNoCase("OBJECTS.3") == 0) ok = OpenDefinitions(3, false);
		else if(base.CompareNoCase("SND.DAT") == 0) ok = OpenDat(true);
		else if(base.CompareNoCase("UIF.DAT") == 0) ok = OpenDat(false);
		else if(base.CompareNoCase("ENDING.FLI") == 0) ok = OpenFli();
		else if(base.CompareNoCase("GAME.PAL") == 0) ok = OpenPalette();

		if(!ok)
			return false;

		NumLumps = Lumps.Size();
		if(!quiet)
			Printf(", %u Nitemare 3D lumps\n", NumLumps);
		return true;
	}

private:
	TArray<FResourceLump *> Lumps;

	void AddRaw(const FString &name, int position, int size)
	{
		FUncompressedLump *lump = new FUncompressedLump;
		lump->Owner = this;
		lump->Position = position;
		lump->LumpSize = size;
		lump->LumpNameSetup(name);
		Lumps.Push(lump);
	}

	void AddMarker(const FString &name)
	{
		AddRaw(name, 0, 0);
	}

	void AddPcm(const FString &name, int position, int size)
	{
		FNitemarePcmLump *lump = new FNitemarePcmLump;
		lump->Owner = this;
		lump->Position = position;
		lump->RawSize = size;
		lump->LumpSize = size + 44;
		lump->LumpNameSetup(name);
		Lumps.Push(lump);
	}

	bool OpenImg(int episode)
	{
		static const int DirectoryBytes = 0x800;
		static const int DirectoryEntries = 256;

		const long length = Reader->GetLength();
		if(length < DirectoryBytes)
			return false;

		BYTE directories[DirectoryBytes];
		Reader->Seek(0, SEEK_SET);
		if(Reader->Read(directories, sizeof(directories)) != sizeof(directories))
			return false;

		const DWORD firstData = ReadLittleLong(directories + 4);
		if(firstData < DirectoryBytes || firstData >= static_cast<DWORD>(length))
			return false;

		FString marker;
		marker.Format("NITIMG%d", episode);
		AddMarker(marker);

		FString headerName;
		headerName.Format("N%dIHDR", episode);
		AddRaw(headerName, 0, static_cast<int>(firstData));

		TArray<DWORD> framePositions;
		TArray<DWORD> frameSizes;

		DWORD pos = firstData;
		unsigned int frame = 0;
		while(pos < static_cast<DWORD>(length))
		{
			if(pos + 10 > static_cast<DWORD>(length))
				return false;

			BYTE frameHeader[10];
			Reader->Seek(pos, SEEK_SET);
			if(Reader->Read(frameHeader, sizeof(frameHeader)) != sizeof(frameHeader))
				return false;

			const unsigned int width = frameHeader[0];
			const unsigned int height = frameHeader[1];
			if(width == 0 || height == 0)
				return false;

			const DWORD frameSize = 10 + width * height;
			if(frameSize > static_cast<DWORD>(length) - pos)
				return false;

			FString frameName;
			frameName.Format("graphics/N%dI%04u.n3i", episode, frame);
			AddRaw(frameName, static_cast<int>(pos), static_cast<int>(frameSize));
			framePositions.Push(pos);
			frameSizes.Push(frameSize);

			pos += frameSize;
			++frame;
		}

		if(frame == 0)
			return false;

		// The first 256 dwords are the wall-image directory and the second
		// 256 dwords are the object-image directory. Only create an alias when
		// the directory offset points at an exact physical frame boundary.
		// This keeps uncertain sequence-bank semantics out of the resource layer.
		for(int objectDirectory = 0; objectDirectory <= 1; ++objectDirectory)
		{
			const int directoryBase = objectDirectory ? 0x400 : 0;
			for(int id = 0; id < DirectoryEntries; ++id)
			{
				const DWORD imageOffset = ReadLittleLong(directories + directoryBase + id * 4);
				if(imageOffset < firstData || imageOffset >= static_cast<DWORD>(length))
					continue;

				for(unsigned int f = 0; f < framePositions.Size(); ++f)
				{
					if(framePositions[f] != imageOffset)
						continue;

					FString alias;
					if(objectDirectory)
						alias.Format("sprites/N%dO%02X.n3i", episode, id);
					else
						alias.Format("textures/N%dW%02X.n3i", episode, id);

					AddRaw(alias, static_cast<int>(imageOffset), static_cast<int>(frameSizes[f]));
					break;
				}
			}
		}

		return true;
	}

	bool OpenMap(int episode)
	{
		static const int HeaderSize = 514;
		static const int LevelBytes = 8192;
		static const int ConvertedLevelBytes = 34 + 3 * 64 * 64 * 2;

		const long length = Reader->GetLength();
		if(length < HeaderSize)
			return false;

		BYTE countBytes[2];
		Reader->Seek(0, SEEK_SET);
		if(Reader->Read(countBytes, 2) != 2)
			return false;
		const WORD declaredCount = ReadLittleShort(countBytes);

		const long payload = length - HeaderSize;
		if(payload < 0 || payload % LevelBytes != 0)
			return false;
		const unsigned int actualCount = static_cast<unsigned int>(payload / LevelBytes);
		if(declaredCount != actualCount)
			return false;

		for(unsigned int i = 0; i < actualCount; ++i)
		{
			FString marker;
			marker.Format("N%dM%02u", episode, i + 1);
			AddMarker(marker);

			FNitemareMapLump *planes = new FNitemareMapLump;
			planes->Owner = this;
			planes->Position = HeaderSize + i * LevelBytes;
			planes->Episode = episode;
			planes->Level = i + 1;
			planes->LumpSize = ConvertedLevelBytes;
			planes->LumpNameSetup("PLANES");
			Lumps.Push(planes);
		}

		FString headerName;
		headerName.Format("N%dMHDR", episode);
		AddRaw(headerName, 0, HeaderSize);
		return actualCount > 0;
	}

	bool OpenDefinitions(int episode, bool walls)
	{
		const long length = Reader->GetLength();
		if(length <= 0 || !ValidateDefinitionTable(Reader))
			return false;

		FString marker;
		marker.Format(walls ? "NITWAL%d" : "NITOBJ%d", episode);
		AddMarker(marker);

		FString tableName;
		tableName.Format(walls ? "N%dWDEF" : "N%dODEF", episode);
		AddRaw(tableName, 0, static_cast<int>(length));
		return true;
	}

	bool OpenDat(bool soundArchive)
	{
		const long length = Reader->GetLength();
		if(length < 6)
			return false;

		BYTE firstDescriptor[6];
		Reader->Seek(0, SEEK_SET);
		if(Reader->Read(firstDescriptor, sizeof(firstDescriptor)) != sizeof(firstDescriptor))
			return false;

		const DWORD firstOffset = ReadLittleLong(firstDescriptor + 2);
		if(firstOffset < 6 || firstOffset > static_cast<DWORD>(length) || (firstOffset % 6) != 0)
			return false;

		const unsigned int slots = firstOffset / 6;
		if(slots == 0)
			return false;

		AddMarker(soundArchive ? "NITSND" : "NITUIF");

		for(unsigned int i = 0; i < slots; ++i)
		{
			BYTE descriptor[6];
			Reader->Seek(i * 6, SEEK_SET);
			if(Reader->Read(descriptor, sizeof(descriptor)) != sizeof(descriptor))
				return false;

			const WORD itemLength = ReadLittleShort(descriptor);
			const DWORD offset = ReadLittleLong(descriptor + 2);
			if(itemLength != 0 &&
				(offset > static_cast<DWORD>(length) ||
				 itemLength > static_cast<DWORD>(length) - offset))
				return false;

			BYTE sig[20];
			memset(sig, 0, sizeof(sig));
			const unsigned int sigLen = itemLength < sizeof(sig) ? itemLength : sizeof(sig);
			if(sigLen)
			{
				Reader->Seek(offset, SEEK_SET);
				if(Reader->Read(sig, sigLen) != static_cast<long>(sigLen))
					return false;
			}

			const bool midi = sigLen >= 4 && memcmp(sig, "MThd", 4) == 0;
			const bool voc = sigLen >= 20 && memcmp(sig, "Creative Voice File", 19) == 0 && sig[19] == 0x1A;
			const bool ibk = sigLen >= 3 && memcmp(sig, "IBK", 3) == 0;
			const bool pcx = sigLen >= 1 && sig[0] == 0x0A;

			FString name;
			if(soundArchive)
			{
				if(midi)
				name.Format("music/NTS%04u.mid", i);
				else if(voc)
					name.Format("sounds/NTS%04u.voc", i);
				else if(ibk || itemLength == 0)
					name.Format("NTS%04u", i);
				else
				{
					name.Format("sounds/NTS%04u.wav", i);
					AddPcm(name, static_cast<int>(offset), itemLength);
					continue;
				}
			}
			else
			{
				if(pcx)
					name.Format("graphics/NTU%04u.pcx", i);
				else if(midi)
					name.Format("music/NTU%04u.mid", i);
				else if(voc)
					name.Format("sounds/NTU%04u.voc", i);
				else
					name.Format("NTU%04u", i);
			}

			AddRaw(name, static_cast<int>(offset), itemLength);
		}
		return true;
	}

	bool OpenFli()
	{
		const long length = Reader->GetLength();
		if(length < 128)
			return false;

		BYTE header[20];
		Reader->Seek(0, SEEK_SET);
		if(Reader->Read(header, sizeof(header)) != sizeof(header))
			return false;

		const WORD magic = ReadLittleShort(header + 4);
		const WORD width = ReadLittleShort(header + 8);
		const WORD height = ReadLittleShort(header + 10);
		const WORD depth = ReadLittleShort(header + 12);
		if((magic != 0xAF11 && magic != 0xAF12) || width == 0 || height == 0 || depth != 8)
			return false;

		AddMarker("NITFLI");
		AddRaw("graphics/ENDINGFL.fli", 0, static_cast<int>(length));
		return true;
	}

	bool OpenPalette()
	{
		const long length = Reader->GetLength();
		if(length < 769)
			return false;

		BYTE marker;
		Reader->Seek(length - 769, SEEK_SET);
		if(Reader->Read(&marker, 1) != 1 || marker != 0x0C)
			return false;

		AddMarker("NITPAL");
		AddRaw("graphics/GAMEPAL.pcx", 0, static_cast<int>(length));
		AddRaw("NITPAL8", static_cast<int>(length - 768), 768);
		return true;
	}
};

static bool IsNitemareFilename(const char *filename)
{
	const FString base = BaseNameOf(filename);
	return base.CompareNoCase("IMG.1") == 0 ||
		base.CompareNoCase("IMG.2") == 0 ||
		base.CompareNoCase("IMG.3") == 0 ||
		base.CompareNoCase("MAP.1") == 0 ||
		base.CompareNoCase("MAP.2") == 0 ||
		base.CompareNoCase("MAP.3") == 0 ||
		base.CompareNoCase("WALLS.1") == 0 ||
		base.CompareNoCase("WALLS.2") == 0 ||
		base.CompareNoCase("WALLS.3") == 0 ||
		base.CompareNoCase("OBJECTS.1") == 0 ||
		base.CompareNoCase("OBJECTS.2") == 0 ||
		base.CompareNoCase("OBJECTS.3") == 0 ||
		base.CompareNoCase("SND.DAT") == 0 ||
		base.CompareNoCase("UIF.DAT") == 0 ||
		base.CompareNoCase("ENDING.FLI") == 0 ||
		base.CompareNoCase("GAME.PAL") == 0;
}

} // namespace

FResourceFile *CheckNitemare(const char *filename, FileReader *file, bool quiet)
{
	if(!IsNitemareFilename(filename))
		return NULL;

	FNitemareResourceFile *rf = new FNitemareResourceFile(filename, file);
	if(rf->Open(quiet))
		return rf;

	rf->Reader = NULL; // caller owns the reader when probing fails
	delete rf;
	return NULL;
}
