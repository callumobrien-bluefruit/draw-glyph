#include "freetype2/ft2build.h"
#include FT_FREETYPE_H
#include "json-c/json.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FACES 32
#define MAX_PATH_LEN 256
#define MAX_SPEC_LEN 512
#define MAX_GLYPH_WIDTH 256
#define MAX_GLYPH_HEIGHT 256

struct glyph_spec {
	char font_path[MAX_PATH_LEN];
	long char_id, pixel_size, width, height, origin_x, origin_y;
};

struct bitmap {
	unsigned long width, height;
	char pixels[MAX_GLYPH_HEIGHT][MAX_GLYPH_WIDTH];
};

static void die(const char *msg);
static int load_ttc(char *path, FT_Face faces[MAX_FACES]);
static bool render_glyph(FT_Face faces[],
                         int face_count,
                         const struct glyph_spec *spec,
                         struct bitmap *glyph);
static bool draw(FT_GlyphSlot slot,
                 const struct glyph_spec *spec,
                 struct bitmap *glyph);
static bool output_pgm(const char *path, const struct bitmap *image);
static bool read_spec(const char *path, struct glyph_spec *spec);
static int read_all(const char *path, char *buffer, int buffer_len);
static bool extract_spec_value(const struct json_object_iterator *iter,
                               struct glyph_spec *spec);
static bool extract_long(const struct json_object *object, long *out);

static FT_Library library;

int main(int argc, char *argv[])
{
	FT_Error error;
	FT_Face faces[MAX_FACES];
	struct glyph_spec spec;
	struct bitmap glyph = { 0 };

	if (argc < 3)
		die("usage: draw-glyph GLYPHSPEC OUTFILE");

	if (!read_spec(argv[1], &spec))
		die("failed to read glyph spec");

	error = FT_Init_FreeType(&library);
	if (error)
		die("failed to initialise FreeType");

	const int face_count = load_ttc(spec.font_path, faces);
	if (face_count < 0)
		die("failed to read font file");

	if (!render_glyph(faces, face_count, &spec, &glyph))
		die("failed to render glyph");

	if (!output_pgm(argv[2], &glyph))
		die("failed to output PGM");

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

/// Loops through `faces` looking for a typeface with a glyph for
/// `spec->char_id`. If one is found, it is rendered and drawn onto
/// `glyph` and `true` is returned; if an error occurs while rendering
/// the glyph or no glyph is found, `false` is returned.
static bool render_glyph(FT_Face faces[],
                         int face_count,
                         const struct glyph_spec *spec,
                         struct bitmap *glyph)
{
	FT_Error error;
	FT_Vector origin;
	bool found_glyph = false;
	int i, glyph_index;

	for (i = 0; i < face_count; ++i) {
		glyph_index = FT_Get_Char_Index(faces[i], spec->char_id);
		if (glyph_index == 0)
			continue; // glyph is not in this face

		found_glyph = true;
		break;
	}

	if (!found_glyph)
		return false;

	error = FT_Set_Pixel_Sizes(faces[i], 0, spec->pixel_size);
	if (error)
		return false;

	origin.x = 64 * spec->origin_x;
	origin.y = 64 * (spec->height - spec->origin_y);
	FT_Set_Transform(faces[i], NULL, &origin);

	error = FT_Load_Glyph(faces[i], glyph_index, FT_LOAD_RENDER);
	if (error)
		return false;

	return draw(faces[i]->glyph, spec, glyph);
}

/// Draws the glyph from `slot` into `glyph` at the position given in
/// `spec`. Returns `true` on success, `false` on error.
static bool draw(FT_GlyphSlot slot,
                 const struct glyph_spec *spec,
                 struct bitmap *glyph)
{
	unsigned offset;
	FT_Bitmap *bitmap = &slot->bitmap;
	unsigned left = slot->bitmap_left;
	unsigned top = spec->height - slot->bitmap_top;

	glyph->width = spec->width;
	glyph->height = spec->height;

	for (unsigned y = 0; y < bitmap->rows; ++y) {
		for (unsigned x = 0; x < bitmap->width; ++x) {
			offset = y * bitmap->pitch + x;
			if (left + x >= glyph->width || top + y >= glyph->height)
				return false;
			glyph->pixels[top + y][left + x] = bitmap->buffer[offset];
		}
	}

	return true;
}

/// Writes `image` to `path` in PGM format. Returns `true` on
/// success, `false` on error
static bool output_pgm(const char *path, const struct bitmap *image)
{
	FILE *stream = fopen(path, "wb");
	if (stream == NULL)
		return false;

	fprintf(stream, "P5\n%ld\n%ld\n255\n", image->width, image->height);
	for (unsigned y = 0; y < image->height; ++y) {
		for (unsigned x = 0; x < image->width; ++x) {
			if (fputc(image->pixels[y][x], stream) == EOF)
				return false;
		}
	}

	return true;
}

/// Reads a glyph spec from `path` and writes the options to `spec`,
/// returning `true` on success, `false` on error.
static bool read_spec(const char *path, struct glyph_spec *spec)
{
	char buffer[MAX_SPEC_LEN];
	struct json_tokener *tokener;
	enum json_tokener_error error;
	struct json_object *spec_object;
	struct json_object_iterator iter, iter_end;

	int spec_len = read_all(path, buffer, MAX_SPEC_LEN);
	if (spec_len < 0)
		return false;

	tokener = json_tokener_new();
	if (tokener == NULL)
		return false;
	do {
		spec_object = json_tokener_parse_ex(tokener, buffer, spec_len);
		error = json_tokener_get_error(tokener);
	} while (error == json_tokener_continue);
	if (error != json_tokener_success)
		return false;

	iter = json_object_iter_begin(spec_object);
	iter_end = json_object_iter_end(spec_object);
	while (!json_object_iter_equal(&iter, &iter_end)) {
		if (!extract_spec_value(&iter, spec))
			return false;
		json_object_iter_next(&iter);
	}

	json_tokener_free(tokener);
	return true;
}

/// Reads the entirety of the file at `path` into `buffer`, returning
/// the number of characters read on success, `-1` on error. Note that
/// `buffer` will not be null-terminated after this operation.
static int read_all(const char *path, char *buffer, int buffer_len)
{
	FILE *file;
	int c, buffer_pos = 0;

	file = fopen(path, "r");
	if (file == NULL)
		return -1;

	while ((c = fgetc(file)) != EOF) {
		if (buffer_pos == buffer_len) {
			fclose(file);
			return -1;
		}

		buffer[buffer_pos++] = (char)c;
	}

	return buffer_pos;
}

/// Extracts the value for the spec property pointed to by `iter` and
/// writes it to the appropriate field in `spec`, returning `true` on
/// success, `false` on error.
static bool extract_spec_value(const struct json_object_iterator *iter,
                               struct glyph_spec *spec)
{
	const char *property_name = json_object_iter_peek_name(iter);
	struct json_object *value = json_object_iter_peek_value(iter);
	if (property_name == NULL || value == NULL)
		return false;

	if (strcmp(property_name, "font-path") == 0) {
		if (json_object_get_type(value) != json_type_string)
			return false;
		const char *path = json_object_get_string(value);
		if (strlen(path) >= MAX_PATH_LEN)
			return false;
		strcpy(spec->font_path, path);
		return true;
	}

	if (strcmp(property_name, "char-id") == 0)
		return extract_long(value, &spec->char_id);

	if (strcmp(property_name, "pixel-size") == 0)
		return extract_long(value, &spec->pixel_size);

	if (strcmp(property_name, "width") == 0)
		return extract_long(value, &spec->width);

	if (strcmp(property_name, "height") == 0)
		return extract_long(value, &spec->height);

	if (strcmp(property_name, "origin-x") == 0)
		return extract_long(value, &spec->origin_x);

	if (strcmp(property_name, "origin-y") == 0)
		return extract_long(value, &spec->origin_y);

	return false;
}

/// Extracts a `long` value from a json object, writing it to `out`.
/// Returns `true` on success, `false` on error.
static bool extract_long(const struct json_object *object, long *out)
{
	if (json_object_get_type(object) != json_type_int)
		return false;
	*out = json_object_get_int(object);
	return true;
}
