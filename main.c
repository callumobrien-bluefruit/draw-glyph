#include <stdlib.h>

#include "freetype2/ft2build.h"
#include FT_FREETYPE_H

static void die(const char *msg);

int main(void)
{
	FT_Error error;
	FT_Library library;

	error = FT_Init_FreeType(&library);
	if (error)
		die("failed to initialise FreeType");

	return 0;
}

/// Prints `msg` to `stderr` then exits with status 1
static void die(const char *msg)
{
	fprintf(stderr, "draw-glyph: %s\n", msg);
	exit(1);
}
