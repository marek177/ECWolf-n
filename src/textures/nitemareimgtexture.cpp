/*
** nitemareimgtexture.cpp
**
** Texture decoder for physical records from Nitemare 3D IMG.1/2/3.
** Records are already stored column-major, matching ECWolf's software
** texture layout.
*/

#include "files.h"
#include "v_palette.h"
#include "w_wad.h"
#include "textures.h"
#include "zstring.h"

class FNitemareImgTexture : public FTexture
{
public:
	FNitemareImgTexture(int lumpnum, FileReader &file);
	~FNitemareImgTexture();

	const BYTE *GetColumn(unsigned int column, const Span **spans_out);
	const BYTE *GetPixels();
	void Unload();

protected:
	BYTE *Pixels;
	Span **Spans;
	bool MaskIndex31;

	void MakeTexture();
};

static bool CheckIfNitemareImg(FileReader &file, int lumpnum)
{
	const char *fullName = Wads.GetLumpFullName(lumpnum);
	if(fullName == NULL)
		return false;

	FString name(fullName);
	if(name.Right(4).CompareNoCase(".n3i") != 0)
		return false;

	if(file.GetLength() < 10)
		return false;

	BYTE header[10];
	file.Seek(0, SEEK_SET);
	if(file.Read(header, sizeof(header)) != sizeof(header))
		return false;

	const unsigned int width = header[0];
	const unsigned int height = header[1];
	if(width == 0 || height == 0)
		return false;

	return file.GetLength() == static_cast<long>(10 + width * height);
}

FTexture *NitemareImgTexture_TryCreate(FileReader &file, int lumpnum)
{
	if(!CheckIfNitemareImg(file, lumpnum))
		return NULL;
	return new FNitemareImgTexture(lumpnum, file);
}

FNitemareImgTexture::FNitemareImgTexture(int lumpnum, FileReader &file)
	: FTexture(NULL, lumpnum), Pixels(NULL), Spans(NULL), MaskIndex31(false)
{
	BYTE header[10];
	file.Seek(0, SEEK_SET);
	file.Read(header, sizeof(header));

	Width = header[0];
	Height = header[1];
	LeftOffset = 0;
	TopOffset = 0;

	// Nitemare 3D uses palette index 31 as sprite transparency. Physical IMG
	// records exposed by the resource reader are graphics by default, so this
	// only becomes active for explicit sprite aliases added later.
	MaskIndex31 = Wads.GetLumpNamespace(lumpnum) == ns_sprites;
	bMasked = MaskIndex31;
	CalcBitSize();
}

FNitemareImgTexture::~FNitemareImgTexture()
{
	Unload();
	if(Spans != NULL)
	{
		FreeSpans(Spans);
		Spans = NULL;
	}
}

void FNitemareImgTexture::Unload()
{
	if(Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FNitemareImgTexture::GetPixels()
{
	if(Pixels == NULL)
		MakeTexture();
	return Pixels;
}

const BYTE *FNitemareImgTexture::GetColumn(unsigned int column, const Span **spans_out)
{
	if(Pixels == NULL)
		MakeTexture();

	if(column >= static_cast<unsigned int>(Width))
	{
		if(WidthMask + 1 == Width)
			column &= WidthMask;
		else
			column %= Width;
	}

	if(spans_out != NULL)
	{
		if(Spans == NULL)
			Spans = CreateSpans(Pixels);
		*spans_out = Spans[column];
	}
	return Pixels + column * Height;
}

void FNitemareImgTexture::MakeTexture()
{
	FWadLump lump = Wads.OpenLumpNum(SourceLump);
	lump.Seek(10, SEEK_SET);

	const unsigned int count = Width * Height;
	BYTE *raw = new BYTE[count];
	Pixels = new BYTE[count];
	lump.Read(raw, count);

	for(unsigned int i = 0; i < count; ++i)
	{
		if(MaskIndex31 && raw[i] == 31)
			Pixels[i] = 0;
		else
			Pixels[i] = GPalette.Remap[raw[i]];
	}
	delete[] raw;
}
