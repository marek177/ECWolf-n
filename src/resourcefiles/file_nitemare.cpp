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

static bool DefinitionTokenEquals(const char *begin, const char *end, const char *text)
{
	const size_t length = static_cast<size_t>(end - begin);
	return strlen(text) == length && strnicmp(begin, text, length) == 0;
}

static bool IsOpenWallDefinitionClass(const char *begin, const char *end)
{
	return DefinitionTokenEquals(begin, end, "FLOOR") ||
		DefinitionTokenEquals(begin, end, "TURN") ||
		DefinitionTokenEquals(begin, end, "RETREAT") ||
		DefinitionTokenEquals(begin, end, "SAFESPOT") ||
		DefinitionTokenEquals(begin, end, "ACTIONSPOT") ||
		DefinitionTokenEquals(begin, end, "TRIGGER1") ||
		DefinitionTokenEquals(begin, end, "TRIGGER2");
}

static int BootstrapDoorAxis(const char *begin, const char *end)
{
	if(DefinitionTokenEquals(begin, end, "DOORV") ||
		DefinitionTokenEquals(begin, end, "DOORVC"))
		return 1;
	if(DefinitionTokenEquals(begin, end, "DOORH") ||
		DefinitionTokenEquals(begin, end, "DOORHC"))
		return 2;
	return 0;
}

static int BootstrapLockedDoorAxis(const char *begin, const char *end)
{
	if(DefinitionTokenEquals(begin, end, "DOORVL") ||
		DefinitionTokenEquals(begin, end, "DOORVL2") ||
		DefinitionTokenEquals(begin, end, "DOORVL3"))
		return 1;
	if(DefinitionTokenEquals(begin, end, "DOORHL") ||
		DefinitionTokenEquals(begin, end, "DOORHL2") ||
		DefinitionTokenEquals(begin, end, "DOORHL3"))
		return 2;
	return 0;
}

static int BootstrapRemoteDoorAxis(const char *begin, const char *end)
{
	if(DefinitionTokenEquals(begin, end, "DOORVR"))
		return 1;
	if(DefinitionTokenEquals(begin, end, "DOORHR"))
		return 2;
	return 0;
}

static int BootstrapTransportDoorAxis(const char *begin, const char *end)
{
	if(DefinitionTokenEquals(begin, end, "DOORVI"))
		return 1;
	if(DefinitionTokenEquals(begin, end, "DOORHI"))
		return 2;
	return 0;
}

static bool DefinitionRangeContainsNoCase(const char *begin, const char *end, const char *needle)
{
	const size_t needleLength = strlen(needle);
	if(needleLength == 0 || static_cast<size_t>(end - begin) < needleLength)
		return false;

	for(const char *p = begin; p + needleLength <= end; ++p)
	{
		if(strnicmp(p, needle, needleLength) == 0)
			return true;
	}
	return false;
}

static int DefinitionDescriptionGroup(const char *begin, const char *end)
{
	for(const char *p = begin; p < end; ++p)
	{
		if(*p != '#')
			continue;

		++p;
		int group = 0;
		bool haveDigit = false;
		while(p < end && *p >= '0' && *p <= '9')
		{
			haveDigit = true;
			group = group * 10 + (*p - '0');
			++p;
		}
		if(haveDigit)
			return group;
		break;
	}
	return 0;
}

static int BootstrapColorLock(const char *begin, const char *end)
{
	// Called only for the verified colored locked-door families. Different
	// episode catalogs spell the same variants as "red key", "Locked Red Door"
	// or "red - locked", so the color word itself is the stable discriminator.
	if(DefinitionRangeContainsNoCase(begin, end, "red")) return 201;
	if(DefinitionRangeContainsNoCase(begin, end, "green")) return 202;
	if(DefinitionRangeContainsNoCase(begin, end, "blue")) return 203;
	if(DefinitionRangeContainsNoCase(begin, end, "yellow")) return 204;
	return 0;
}

static int BootstrapTransportLock(const char *begin, const char *end)
{
	if(DefinitionRangeContainsNoCase(begin, end, "door 1")) return 205;
	if(DefinitionRangeContainsNoCase(begin, end, "door 2")) return 206;
	return 0;
}

static int BootstrapKeyPassageLock(const char *begin, const char *end)
{
	if(DefinitionTokenEquals(begin, end, "WARP_L1")) return 201;
	if(DefinitionTokenEquals(begin, end, "WARP_L2")) return 202;
	if(DefinitionTokenEquals(begin, end, "WARP_L3")) return 203;
	if(DefinitionTokenEquals(begin, end, "WARP_L4")) return 204;
	return 0;
}

static int BootstrapClimbGroup(const char *begin, const char *end)
{
	if(end - begin == 6 && strnicmp(begin, "WARP_", 5) == 0 &&
		begin[5] >= '1' && begin[5] <= '8')
		return begin[5] - '0';
	return 0;
}

static int BootstrapElevatorGroup(const char *begin, const char *end)
{
	if(end - begin == 7 && strnicmp(begin, "WARP_E", 6) == 0 &&
		begin[6] >= '1' && begin[6] <= '8')
		return begin[6] - '0';
	return 0;
}

static int DefinitionIdClimbGroup(const char *data, long length, unsigned int wantedId)
{
	const char *p = data;
	const char *end = data + length;
	while(p < end)
	{
		const char *line = p;
		while(p < end && *p != '\n')
			++p;
		const char *lineEnd = p;
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
		bool complete = true;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				complete = false;
				break;
			}
			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}
		if(!complete)
			continue;

		unsigned int id;
		if(ParseDefinitionId(tokens[0], tokens[1], id) && id == wantedId)
			return BootstrapClimbGroup(tokens[6], tokens[7]);
	}
	return 0;
}

static int DefinitionIdElevatorGroup(const char *data, long length, unsigned int wantedId)
{
	const char *p = data;
	const char *end = data + length;
	while(p < end)
	{
		const char *line = p;
		while(p < end && *p != '\n')
			++p;
		const char *lineEnd = p;
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
		bool complete = true;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				complete = false;
				break;
			}
			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}
		if(!complete)
			continue;

		unsigned int id;
		if(ParseDefinitionId(tokens[0], tokens[1], id) && id == wantedId)
			return BootstrapElevatorGroup(tokens[6], tokens[7]);
	}
	return 0;
}


static int DefinitionMinIdForClassRange(const char *data, long length,
	const char *wantedBegin, const char *wantedEnd)
{
	const char *p = data;
	const char *end = data + length;
	int minimum = -1;

	while(p < end)
	{
		const char *line = p;
		while(p < end && *p != '\n')
			++p;
		const char *lineEnd = p;
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
		bool complete = true;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				complete = false;
				break;
			}
			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}
		if(!complete)
			continue;

		const ptrdiff_t wantedLength = wantedEnd - wantedBegin;
		if(tokens[7] - tokens[6] != wantedLength ||
			strnicmp(tokens[6], wantedBegin, wantedLength) != 0)
		{
			continue;
		}

		unsigned int id;
		if(ParseDefinitionId(tokens[0], tokens[1], id) &&
			(minimum < 0 || id < static_cast<unsigned int>(minimum)))
		{
			minimum = static_cast<int>(id);
		}
	}
	return minimum;
}

static int DefinitionMinIdForClassName(const char *data, long length, const char *className)
{
	const char *p = data;
	const char *end = data + length;
	int minimum = -1;

	while(p < end)
	{
		const char *line = p;
		while(p < end && *p != '\n')
			++p;
		const char *lineEnd = p;
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
		bool complete = true;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				complete = false;
				break;
			}
			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}
		if(!complete || !DefinitionTokenEquals(tokens[6], tokens[7], className))
			continue;

		unsigned int id;
		if(ParseDefinitionId(tokens[0], tokens[1], id) &&
			(minimum < 0 || id < static_cast<unsigned int>(minimum)))
			minimum = static_cast<int>(id);
	}

	return minimum;
}
static int DefinitionIdForClassGroup(const char *data, long length,
	const char *className, int wantedGroup)
{
	const char *p = data;
	const char *end = data + length;
	while(p < end)
	{
		const char *line = p;
		while(p < end && *p != '\n')
			++p;
		const char *lineEnd = p;
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
		bool complete = true;
		for(int field = 0; field < 4; ++field)
		{
			while(scan < lineEnd && IsDefinitionSpace(*scan))
				++scan;
			if(scan == lineEnd)
			{
				complete = false;
				break;
			}
			tokens[field * 2] = scan;
			while(scan < lineEnd && !IsDefinitionSpace(*scan))
				++scan;
			tokens[field * 2 + 1] = scan;
		}
		if(!complete || !DefinitionTokenEquals(tokens[6], tokens[7], className))
			continue;

		const char *description = scan;
		while(description < lineEnd && IsDefinitionSpace(*description))
			++description;
		if(DefinitionDescriptionGroup(description, lineEnd) != wantedGroup)
			continue;

		unsigned int id;
		if(ParseDefinitionId(tokens[0], tokens[1], id))
			return static_cast<int>(id);
	}
	return -1;
}


static int RecoveredObjectClassCode(const char *begin, const char *end)
{
	if(end - begin > 5 && strnicmp(begin, "GUARD", 5) == 0)
	{
		int number = 0;
		for(const char *p = begin + 5; p != end; ++p)
		{
			if(*p < '0' || *p > '9')
				return -1;
			number = number * 10 + (*p - '0');
		}
		if(number >= 1 && number <= 26)
			return 0x07 + number;
	}

	if(DefinitionTokenEquals(begin, end, "CAUSTIC")) return 0x07;
	if(DefinitionTokenEquals(begin, end, "SAFE")) return 0x26;
	if(DefinitionTokenEquals(begin, end, "TRUNK")) return 0x27;
	if(DefinitionTokenEquals(begin, end, "PUSH")) return 0x28;
	if(DefinitionTokenEquals(begin, end, "ACTION")) return 0x29;
	if(DefinitionTokenEquals(begin, end, "PERMEABLE")) return 0x2A;
	if(DefinitionTokenEquals(begin, end, "DUMB")) return 0x2B;
	if(DefinitionTokenEquals(begin, end, "ELEVATED")) return 0x2E;
	if(DefinitionTokenEquals(begin, end, "KEY")) return 0x2F;
	if(DefinitionTokenEquals(begin, end, "IDCARD")) return 0x30;
	if(DefinitionTokenEquals(begin, end, "FOOD")) return 0x33;
	if(DefinitionTokenEquals(begin, end, "WEAPON")) return 0x36;
	if(DefinitionTokenEquals(begin, end, "AMMO")) return 0x39;
	if(DefinitionTokenEquals(begin, end, "CRYSTALB")) return 0x3A;
	if(DefinitionTokenEquals(begin, end, "MAGICEYE")) return 0x3B;
	if(DefinitionTokenEquals(begin, end, "PENTAGRAM")) return 0x3C;
	if(DefinitionTokenEquals(begin, end, "SCROLL")) return 0x3D;
	return -1;
}

static bool BuildDefinitionXlat(FileReader *reader, int episode, bool walls, FString &xlat, FString *decorate)
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

	if(walls)
		xlat.Format("include \"N%dOXLAT\"\n\ntiles\n{\n", episode);
	else
	{
		xlat = "things\n{\n";
		if(decorate != NULL)
			decorate->Truncate(0);
	}

	char *p = data;
	char *end = data + length;
	unsigned int records = 0;
	bool valid = true;
	bool playerStartWritten = false;

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

		const char *classBegin = tokens[6];
		const char *classEnd = tokens[7];
		const char *descriptionBegin = scan;
		while(descriptionBegin < lineEnd && IsDefinitionSpace(*descriptionBegin))
			++descriptionBegin;

		if(walls)
		{
			if(id != 0 && !DefinitionTokenEquals(classBegin, classEnd, "NULL"))
			{
				FString lineText;
				if(IsOpenWallDefinitionClass(classBegin, classEnd))
				{
					lineText.Format("\tzone %u {}\n", id);
				}
				else
				{
					FString texture;
					texture.Format("N%dW%02X", episode, id);

					if(DefinitionTokenEquals(classBegin, classEnd, "CONTROL"))
					{
						const int controlGroup = DefinitionDescriptionGroup(descriptionBegin, lineEnd);
						if(controlGroup >= 1 && controlGroup <= 2)
						{
							const int verticalRaw = DefinitionIdForClassGroup(data, length, "DOORVR", controlGroup);
							const int horizontalRaw = DefinitionIdForClassGroup(data, length, "DOORHR", controlGroup);
							if(verticalRaw >= 0 || horizontalRaw >= 0)
							{
								FString controlTrigger;
								controlTrigger.Format(
									"\ttrigger %u\n\t{\n"
									"\t\taction = \"Nitemare_RemoteControl\";\n"
									"\t\targ0 = %u;\n"
									"\t\targ1 = %d;\n"
									"\t\targ2 = %d;\n"
									"\t\targ3 = %d;\n"
									"\t\targ4 = %d;\n"
									"\t\tplayeruse = true;\n"
									"\t}\n",
									id, id, controlGroup - 1,
									verticalRaw, horizontalRaw, 204 + controlGroup);
								xlat += controlTrigger;
							}
						}
					}

					if(DefinitionTokenEquals(classBegin, classEnd, "WARP_S1") ||
						DefinitionTokenEquals(classBegin, classEnd, "WARP_S2"))
					{
						const bool sourcePortal = DefinitionTokenEquals(classBegin, classEnd, "WARP_S1");
						const int targetRaw = sourcePortal ?
							DefinitionMinIdForClassName(data, length, "WARP_S2") : static_cast<int>(id);
						if(!sourcePortal || targetRaw >= 0)
						{
							FString portalTrigger;
							portalTrigger.Format(
								"\ttrigger %u\n\t{\n"
								"\t\taction = \"Nitemare_MirrorPortal\";\n"
								"\t\targ0 = %u;\n"
								"\t\targ1 = %d;\n"
								"\t\targ2 = %d;\n"
								"\t\tplayeruse = true;\n"
								"\t}\n",
								id, id, targetRaw, sourcePortal ? 1 : 2);
							xlat += portalTrigger;
						}
					}

					const int elevatorGroup = BootstrapElevatorGroup(classBegin, classEnd);
					if(elevatorGroup != 0)
					{
						unsigned int minId = id;
						unsigned int maxId = id;
						for(unsigned int candidate = 0; candidate <= 0xFF; ++candidate)
						{
							if(DefinitionIdElevatorGroup(data, length, candidate) == elevatorGroup)
							{
								if(candidate < minId) minId = candidate;
								if(candidate > maxId) maxId = candidate;
							}
						}

						FString elevatorTrigger;
						elevatorTrigger.Format(
							"\ttrigger %u\n\t{\n"
							"\t\taction = \"Nitemare_ElevatorWarp\";\n"
							"\t\targ0 = %u;\n"
							"\t\targ1 = %u;\n"
							"\t\targ2 = %u;\n"
							"\t\tplayeruse = true;\n"
							"\t}\n",
							id, id, minId, maxId);
						xlat += elevatorTrigger;
					}

					const int climbGroup = BootstrapClimbGroup(classBegin, classEnd);
					if(climbGroup != 0)
					{
						unsigned int minId = id;
						unsigned int maxId = id;
						for(unsigned int candidate = 0; candidate <= 0xFF; ++candidate)
						{
							if(DefinitionIdClimbGroup(data, length, candidate) == climbGroup)
							{
								if(candidate < minId) minId = candidate;
								if(candidate > maxId) maxId = candidate;
							}
						}

						FString climbTrigger;
						climbTrigger.Format(
							"\ttrigger %u\n\t{\n"
							"\t\taction = \"Nitemare_ClimbWarp\";\n"
							"\t\targ0 = %u;\n"
							"\t\targ1 = %u;\n"
							"\t\targ2 = %u;\n"
							"\t\tplayeruse = true;\n"
							"\t}\n",
							id, id, minId, maxId);
						xlat += climbTrigger;
					}

					const int keyPassageLock = BootstrapKeyPassageLock(classBegin, classEnd);
					if(keyPassageLock != 0)
					{
						FString passageTrigger;
						passageTrigger.Format(
							"\ttrigger %u\n\t{\n"
							"\t\taction = \"Nitemare_KeyPassage\";\n"
							"\t\targ0 = %d;\n"
							"\t\tplayeruse = true;\n"
							"\t}\n",
							id, keyPassageLock);
						xlat += passageTrigger;
					}

					if(DefinitionTokenEquals(classBegin, classEnd, "LEVEL_UP") ||
						DefinitionTokenEquals(classBegin, classEnd, "LEVEL_UP2"))
					{
						FString exitTrigger;
						exitTrigger.Format(
							"\ttrigger %u\n\t{\n"
							"\t\taction = \"%s\";\n"
							"\t\tplayeruse = true;\n"
							"\t}\n",
							id,
							DefinitionTokenEquals(classBegin, classEnd, "LEVEL_UP2") ?
								"Nitemare_LevelUp2" : "Exit_Normal");
						xlat += exitTrigger;
					}

					const int ordinaryDoorAxis = BootstrapDoorAxis(classBegin, classEnd);
					const int lockedDoorAxis = BootstrapLockedDoorAxis(classBegin, classEnd);
					const int remoteDoorAxis = BootstrapRemoteDoorAxis(classBegin, classEnd);
					const int transportDoorAxis = BootstrapTransportDoorAxis(classBegin, classEnd);
					const int doorAxis = ordinaryDoorAxis != 0 ? ordinaryDoorAxis :
						lockedDoorAxis != 0 ? lockedDoorAxis :
						remoteDoorAxis != 0 ? remoteDoorAxis : transportDoorAxis;
					const int lock = lockedDoorAxis != 0 ? BootstrapColorLock(descriptionBegin, lineEnd) :
						transportDoorAxis != 0 ? BootstrapTransportLock(descriptionBegin, lineEnd) : 0;

					if(ordinaryDoorAxis != 0 ||
						(lockedDoorAxis != 0 && lock != 0) ||
						(transportDoorAxis != 0 && lock != 0))
					{
						FString lockArg;
						if(lock != 0)
							lockArg.Format("\t\targ3 = %d;\n", lock);

						FString directionArg;
						if(doorAxis == 2)
							directionArg = "\t\targ4 = 1;\n";

						FString trigger;
						trigger.Format(
							"\ttrigger %u\n\t{\n"
							"\t\taction = \"Door_Open\";\n"
							"\t\targ1 = 16;\n"
							"\t\targ2 = 300;\n"
							"%s"
							"%s"
							"\t\tplayeruse = true;\n"
							"\t\trepeatable = true;\n"
							"%s"
							"\t}\n",
							id, lockArg.GetChars(), directionArg.GetChars(),
							doorAxis == 1 ?
								"\t\tactivatenorth = false;\n\t\tactivatesouth = false;\n" :
								"\t\tactivateeast = false;\n\t\tactivatewest = false;\n");
						xlat += trigger;
					}

					lineText.Format(
						"\ttile %u\n\t{\n"
						"\t\ttexturenorth = \"%s\";\n"
						"\t\ttexturesouth = \"%s\";\n"
						"\t\ttextureeast = \"%s\";\n"
						"\t\ttexturewest = \"%s\";\n"
						"%s"
						"\t}\n",
						id, texture.GetChars(), texture.GetChars(),
						texture.GetChars(), texture.GetChars(),
						doorAxis == 1 ? "\t\toffsetvertical = true;\n" :
							doorAxis == 2 ? "\t\toffsethorizontal = true;\n" : "");
				}
				xlat += lineText;
			}
		}
		else
		{
			if(DefinitionTokenEquals(classBegin, classEnd, "START"))
			{
				if(!playerStartWritten && id == 1)
				{
					// Nitemare uses IDs 1..4 for N/E/S/W starts, the same four-way
					// ordered range expected by ECWolf's player-start translation.
					xlat += "\t{1, $Player1Start, 4, 0, 0}\n";
					playerStartWritten = true;
				}
			}
			else
			{
				const int objectClass = RecoveredObjectClassCode(classBegin, classEnd);
				if(id != 0 && objectClass >= 0x06)
				{
					FString actorName;
					actorName.Format("N3DE%dO%02X", episode, id);

					FString xlatLine;
					xlatLine.Format("\t{%u, %s, 0, 0, 0}\n", id, actorName.GetChars());
					xlat += xlatLine;

					if(decorate != NULL)
					{
						FString sprite;
						sprite.Format("N%d%02X", episode, id);

						const bool guardActor = objectClass >= 0x08 && objectClass <= 0x21;
						const int guardBaseId = guardActor ?
							DefinitionMinIdForClassRange(data, length, classBegin, classEnd) : -1;
						const int guardVariant =
							guardBaseId >= 0 ? static_cast<int>(id) - guardBaseId : 0;
						const bool keyInventory = objectClass == 0x2F || objectClass == 0x30;
						const bool pentagramInventory = objectClass == 0x3C;
						const bool healthPickup = objectClass == 0x33;
						const bool weaponPickup = objectClass == 0x36 && id >= 0x25 && id <= 0x28;
						const bool ammoPickup = objectClass == 0x39;
						const bool mapPowerPickup = objectClass == 0x3A || objectClass == 0x3B;

						FString parent;
						FString properties;
						if(guardActor)
						{
							parent.Format(" : NitemareGuardClass%02X", objectClass);
						}
						else if(keyInventory)
						{
							parent = " : Key";
							properties = "\t+INVENTORY.ALWAYSPICKUP\n";
						}
						else if(pentagramInventory)
						{
							parent = " : Inventory";
							properties =
								"\t+INVENTORY.ALWAYSPICKUP\n"
								"\tinventory.interhubamount 1\n";
						}
						else if(healthPickup)
						{
							parent = " : Health";
							const int subtype = id >= 0x12 ? id - 0x12 : 0;
							const int amount = subtype < 5 ? 20 >> subtype : 0;
							properties.Format(
								"\tinventory.amount %d\n"
								"\tinventory.maxamount 100\n",
								amount);
						}
						else if(weaponPickup)
						{
							parent.Format(" : NitemareWeaponPickup%u", id - 0x25);
						}
						else if(ammoPickup)
						{
							if(id == 0x29)
								parent = " : NitemareSilverAmmo";
							else if(id == 0x2A)
								parent = " : NitemarePlasmaAmmo";
							else if(id == 0x2B)
								parent = " : NitemareWandAmmo";
						}
						else if(mapPowerPickup)
						{
							parent = objectClass == 0x3A ?
								" : NitemareCrystalCharge" : " : NitemareMagicEyeCharge";
						}

						const bool inventory = keyInventory || pentagramInventory ||
							healthPickup || weaponPickup ||
							(ammoPickup && !parent.IsEmpty()) || mapPowerPickup;
						if(!inventory && !guardActor)
							properties += "\tradius 32\n";

						FString actor;
						const bool hideTerminalGuard =
							objectClass == 0x09 || objectClass == 0x0A ||
							objectClass == 0x12 || objectClass == 0x13 ||
							objectClass == 0x1A || objectClass == 0x1E ||
							objectClass == 0x1F;
						if(guardActor)
						{
							actor.Format(
								"actor %s%s\n"
								"{\n"
								"%s"
								"\t+SOLID\n"
								"\tstates\n"
								"\t{\n"
								"\t\tSpawn:\n"
								"\t\t\tTNT1 A 0 A_NitemareInitGuardClass(%d, %d)\n"
								"\t\t\t%s A -1\n"
								"\t\t\tstop\n"
								"\t\tPain:\n"
								"\t\t\t%s A 1 A_NitemareGuardPainFinalize\n"
								"\t\t\tgoto Spawn\n"
								"\t\tDeath:\n"
								"\t\t\t%s A 1 A_NitemareGuardDeathStep\n"
								"\t\t\tloop\n"
								"\t\tDeathDone:\n"
								"\t\t\t%s A 1 A_NitemareGuardDeathFinalize\n"
								"\t\t\t%s A -1\n"
								"\t\t\tstop\n"
								"\t}\n"
								"}\n\n",
								actorName.GetChars(), parent.GetChars(),
								properties.GetChars(), objectClass, guardVariant,
								sprite.GetChars(), sprite.GetChars(),
								sprite.GetChars(), sprite.GetChars(),
								hideTerminalGuard ? "TNT1" : sprite.GetChars());
						}
						else
						{
							actor.Format(
								"actor %s%s\n"
								"{\n"
								"%s"
								"%s"
								"\tstates\n"
								"\t{\n"
								"\t\tSpawn:\n"
								"\t\t\t%s A -1\n"
								"\t\t\tstop\n"
								"\t}\n"
								"}\n\n",
								actorName.GetChars(), parent.GetChars(),
								properties.GetChars(),
								(objectClass >= 0x08 && objectClass <= 0x2D) ? "\t+SOLID\n" : "",
								sprite.GetChars());
						}
						*decorate += actor;
					}
				}
			}
		}

		++records;
	}

	delete[] data;
	if(!valid || records == 0)
		return false;

	xlat += "}\n";
	return true;
}

class FNitemareMemoryLump : public FResourceLump
{
public:
	FString Data;

protected:
	int FillCache()
	{
		Cache = new char[LumpSize];
		memcpy(Cache, Data.GetChars(), LumpSize);
		RefCount = 1;
		return 1;
	}
};

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

	void AddMemory(const FString &name, const FString &data)
	{
		FNitemareMemoryLump *lump = new FNitemareMemoryLump;
		lump->Owner = this;
		lump->Data = data;
		lump->LumpSize = data.Len();
		lump->LumpNameSetup(name);
		Lumps.Push(lump);
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
						alias.Format("sprites/N%d%02XA0.n3i", episode, id);
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

			const int levelPosition = HeaderSize + i * LevelBytes;
			FNitemareMapLump *planes = new FNitemareMapLump;
			planes->Owner = this;
			planes->Position = levelPosition;
			planes->Episode = episode;
			planes->Level = i + 1;
			planes->LumpSize = ConvertedLevelBytes;
			planes->LumpNameSetup("PLANES");
			Lumps.Push(planes);

			// Preserve the level-initial static ID-card presence mask used by
			// the original elevator floor menu. IDs 09/0A are the stable
			// red/yellow IDCARD object IDs in OBJECTS.1-3.
			BYTE raw[LevelBytes];
			Reader->Seek(levelPosition, SEEK_SET);
			if(Reader->Read(raw, LevelBytes) != LevelBytes)
				return false;

			unsigned int idCardMask = 0;
			for(unsigned int cell = 0; cell < 64 * 64; ++cell)
			{
				const BYTE objectId = raw[cell * 2 + 1];
				if(objectId == 0x09)
					idCardMask |= 0x01;
				else if(objectId == 0x0A)
					idCardMask |= 0x02;
			}

			FString cardMetaName;
			cardMetaName.Format("N%dM%02uIC", episode, i + 1);
			FString cardMeta;
			cardMeta.Format("%u", idCardMask);
			AddMemory(cardMetaName, cardMeta);
		}

		FString headerName;
		headerName.Format("N%dMHDR", episode);
		AddRaw(headerName, 0, HeaderSize);
		return actualCount > 0;
	}

	bool OpenDefinitions(int episode, bool walls)
	{
		const long length = Reader->GetLength();
		FString xlat;
		FString decorate;
		if(length <= 0 || !BuildDefinitionXlat(Reader, episode, walls, xlat, walls ? NULL : &decorate))
			return false;

		FString marker;
		marker.Format(walls ? "NITWAL%d" : "NITOBJ%d", episode);
		AddMarker(marker);

		FString tableName;
		tableName.Format(walls ? "N%dWDEF" : "N%dODEF", episode);
		AddRaw(tableName, 0, static_cast<int>(length));

		FString xlatName;
		xlatName.Format(walls ? "N%dWXLAT" : "N%dOXLAT", episode);
		AddMemory(xlatName, xlat);

		if(!walls && decorate.IsNotEmpty())
			AddMemory("DECORATE", decorate);
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
