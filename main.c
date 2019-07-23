#include <stdio.h>
#include <stdlib.h>

#include "freetype2/ft2build.h"
#include FT_FREETYPE_H

#define MAX_FACES 32

static void die(const char *msg);
static int load_ttc(char *path, FT_Face faces[MAX_FACES]);

static FT_Library library;

int main(void)
{
	FT_Error error;
	FT_Face faces[MAX_FACES];

	error = FT_Init_FreeType(&library);
	if (error)
		die("failed to initialise FreeType");

	const int face_count = load_ttc("SourceHanSans-Regular.ttc", faces);
	if (face_count < 0)
		die("failed to read font file");

	printf("%d\n", face_count);

	for (int i = 0; i < face_count; ++i)
		FT_Done_Face(faces[i]);

	return 0;
}

/// Prints `msg` to `stderr` then exits with status 1
static void die(const char *msg)
{
	fprintf(stderr, "draw-glyph: %s\n", msg);
	exit(1);
}

/// Loads a TTC from `path`, writing each face into `faces` and
/// returning how many there are. Returns `-1` on error.
static int load_ttc(char *path, FT_Face faces[MAX_FACES])
{
	FT_Error error;

	// `face_index` of -1 means just load the metadata
	error = FT_New_Face(library, path, -1, faces);
	if (error)
		return -1;

	const int face_count = faces[0]->num_faces;
	if (face_count > MAX_FACES)
		return -1;

	for (int i = 0; i < face_count; ++i) {
		error = FT_New_Face(library, path, i, &faces[i]);
		if (error)
			return -1;
	}

	return face_count;
}
