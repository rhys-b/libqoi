#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include "qoi.h"

const QoiChannel QOI_CHANNEL_RGBA = 4;
const QoiChannel QOI_CHANNEL_RGB = 3;

const QoiColorspace QOI_COLORSPACE_SRGB = 0;
const QoiColorspace QOI_COLORSPACE_LINEAR = 1;

#define MIN(a,b) (((a)<(b)) * (a) + ((b)<=(a)) * (b))

/**
 * The following series of macros are for testing what operator is indicated
 * by the input byte.
 */
#define IS_QOI_OP_RGB(b)   (b == 0xFE)
#define IS_QOI_OP_RGBA(b)  (b == 0xFF)
#define IS_QOI_OP_INDEX(b) ((b & 0b11000000) == 0)
#define IS_QOI_OP_DIFF(b)  ((b & 0b11000000) == 0b01000000)
#define IS_QOI_OP_LUMA(b)  ((b & 0b11000000) == 0b10000000)
#define IS_QOI_OP_RUN(b)   ((b & 0b11000000) == 0b11000000)

/**
 * Contains the error code for the previous error.
 */
enum
{
	QOI_ERROR_NONE,
	QOI_ERROR_PERMISSIONS,
	QOI_ERROR_MEMORY,
	QOI_ERROR_FILE_CONTENT,
	QOI_ERROR_NOT_QOI_FILE,
	QOI_ERROR_DISK_SPACE,
	QOI_INVALID_ERROR_CODE
};
int qoi_error = QOI_ERROR_NONE;

/**
 * The string representations of the above errors.
 */
static const char *(qoi_strerror_messages)[] = {
	"No error",
	"Insufficient file permissions, or file doesn't exist",
	"Insufficient memory",
	"File could not be read",
	"File is not a valid QOI file",
	"Insufficient disk space to save file",
	"Warning: Not a valid error code"
};

/**
 * Contains the main QOI object that can be operated upon.
 */
typedef struct Qoi
{
	uint32_t width, height;
	QoiColorspace colorspace;
	QoiChannel channels;
	uint8_t *data;
	void (*freer)(void*);
} Qoi;

/**
 * Represents a typical 32-bit RGBA color.
 */
typedef struct
{
	uint8_t r, g, b, a;
} color;

/**
 * Returns 1 if the two colors are equal on all parts, 0 otherwise.
 */
static char color_equal(
		const color c1,
		const color c2);

/**
 * Creates a color from the three our four bytes in INPUT, the size of which is
 * determined by CHANNELS (3 for RGB, 4 for RGBA).
 */
static color create_color(
		const uint8_t *input,
		const QoiChannel channels);

/**
 * Determines the QOI hash of the color, used for indexing into the 'previous
 * colors' array.
 */
static int color_hash(
		const color c);

/**
 * Converts the given four bytes into a 32 bit number by interpreting the bytes
 * as big endian.
 */
static uint32_t big_endian(
		const unsigned char *raw);

/**
 * Stores the 32 bit numerical INPUT into a big endian array OUTPUT.
 */
static void big_endian_r(
		char *output,
		const uint32_t input);

/**
 * Retrieves the size in bytes of the file given by the FILEPATH.
 */
static ssize_t filesize(
		const char *filepath);

/**
 * Decodes the raw file data INPUT into a pixel buffer OUTPUT. Returns 0 on
 * success and -1 on failure. qoi_error is set to indicate the error.
 */
static int decode(
		const unsigned char *input,
		char *output,
		const size_t output_size,
		const char has_alpha_channel);

/**
 * Encodes the pixel data in SELF and writes it into the FILE. The FILE should
 * already be open, and will not be closed by this function. Returns 0 on
 * success and -1 on error, with qoi_error set to indicate why. This does not
 * write the header nor the trailer.
 */
static int encode(
		const Qoi *self,
		FILE *file);

/**
 * Returns the index in the ARRAY that contains the given VALUE.
 */
static int index_of(
		const color *array,
		const size_t arraylen,
		const color value);

/**
 * Returns 1 if the two colors are equal on all parts, 0 otherwise.
 */
static char color_equal(
		const color c1,
		const color c2)
{
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

/**
 * Creates a color from the three our four bytes in INPUT, the size of which is
 * determined by CHANNELS (3 for RGB, 4 for RGBA). The order of the bytes is
 * RGB(A) and if no alpha channel is present, it is set to 255.
 */
static color create_color(
		const uint8_t *input,
		const QoiChannel channels)
{
	color c;
	c.r = input[0];
	c.g = input[1];
	c.b = input[2];
	c.a = channels == QOI_CHANNEL_RGBA ? input[3] : 255;

	return c;
}

/**
 * Determines the QOI hash of the color, used for indexing into the 'previous
 * colors' array.
 */
static int color_hash(
		const color c)
{
		return (c.r * 3 + c.g * 5 + c.b * 7 + c.a * 11) % 64;
}

/**
 * Construct a new initially blank QOI object with certain spectifications.
 * If there is an error, this returns NULL, and qoi_errno() can be used to find
 * out why. The returned object should be freed using qoi_free() when no longer
 * needed.
 */
Qoi *qoi_new(
		uint32_t width,
		uint32_t height,
		QoiColorspace colorspace,
		QoiChannel channels)
{
	return qoi_new_from_data(
			width,
			height,
			colorspace,
			channels,
			malloc(width * height * channels),
			free);
}

/**
 * Construct a new QOI object using the given specifications with some initial
 * image data. The QOI object takes ownership of the data, and will free it
 * using the freeing function when qoi_free() is called. The buffer should be
 * width * height * (3 or 4) bytes, 3 if the colorspace is RGB and 4 if the
 * colorspace is RGBA.
 */
Qoi *qoi_new_from_data(
		uint32_t width,
		uint32_t height,
		QoiColorspace colorspace,
		QoiChannel channels,
		void *image_buffer,
		void (*freeing_function)(void*))
{
	Qoi *self = malloc(sizeof(Qoi));
	self->width = width;
	self->height = height;
	self->channels = channels;
	self->colorspace = colorspace;
	self->freer = freeing_function;
	self->data = image_buffer;
	return self;
}

/**
 * Construct a new QOI object from a QOI file. If the file is not valid,
 * this returns NULL, and qoi_errno() can be used to find out why. The returned
 * object should be freed using qoi_free() when no longer needed.
 */
Qoi *qoi_new_from_file(
		const char *filepath)
{
	/* Get the size of the input file. */
	ssize_t size = filesize(filepath);
	if (size == -1) {
		qoi_error = QOI_ERROR_PERMISSIONS;
		return NULL;
	}

	/* Allocate space for the file header and content. */
	unsigned char *file_buffer = malloc(size);

	if (file_buffer == NULL) {
		if (file_buffer   != NULL) free(file_buffer);
		qoi_error = QOI_ERROR_MEMORY;
		return NULL;
	}

	/* Open the file. */
	FILE *file = fopen(filepath, "rb");
	if (file == NULL) {
		qoi_error = QOI_ERROR_PERMISSIONS;
		free(file_buffer);
		return NULL;
	}

	/* Read the header and file body. */
	size_t bytes_read = fread(file_buffer, 1, size, file);
	fclose(file);
	if (bytes_read != size) {
		qoi_error = QOI_ERROR_FILE_CONTENT;
		free(file_buffer);
		return NULL;
	}

	/* Ensure the file is a QOI file. */
	if (file_buffer[0] != 'q' ||
	    file_buffer[1] != 'o' ||
	    file_buffer[2] != 'i' ||
	    file_buffer[3] != 'f') {

		qoi_error = QOI_ERROR_NOT_QOI_FILE;
		free(file_buffer);
		return NULL;
	}

	/* Get the header attributes, and allocate space for pixel data. */
	uint32_t width = big_endian(file_buffer + 4);
   	uint32_t height = big_endian(file_buffer + 8);
	QoiChannel channels = file_buffer[12];
	QoiColorspace colorspace = file_buffer[13];
	char *pixel_data = malloc(width * height * channels);

	/* Decode the file body into pixel data, and create a QOI object from it. */
	decode(file_buffer + 14,
	       pixel_data,
		   width * height * channels,
	       channels - 3);

	free(file_buffer);

	return qoi_new_from_data(
			width,
			height,
			colorspace,
			channels,
			pixel_data,
			free);
}

/**
 * Releases the resources held by a QOI object. Any buffers held by external
 * resources after this call will be invalid.
 */
void qoi_free(
		Qoi *self)
{
	if (self->freer != NULL) {
		self->freer(self->data);
	}

	free(self);
}

/**
 * Saves a QOI object to a .qoi file. On success returns 0, otherwise returns
 * -1. qoi_errno() can be used to find out why a save operation failed.
 */
int qoi_save(
		const Qoi *self,
		const char *filepath)
{
	/* This macro is used only by this function. */
#define QOI_SAFWRITE(buf,size,file) \
	if (fwrite(buf, 1, size, file) < size) { \
		qoi_error = QOI_ERROR_DISK_SPACE; \
		fclose(file); \
		return -1; \
	}

	/* Attempt to open the file. */
	FILE *file = fopen(filepath, "wb");
	if (file == NULL) {
		qoi_error = QOI_ERROR_PERMISSIONS;
		return -1;
	}

	/* Create header fields in big endian format. */
	char magic[4] = { 'q', 'o', 'i', 'f' };
	char big_endian_width[4], big_endian_height[4];
	big_endian_r(big_endian_width, self->width);
	big_endian_r(big_endian_height, self->height);

	/* Write header. */
	QOI_SAFWRITE(magic, 4, file);
	QOI_SAFWRITE(big_endian_width, 4, file);
	QOI_SAFWRITE(big_endian_height, 4, file);
	QOI_SAFWRITE(&self->channels, 1, file);
	QOI_SAFWRITE(&self->colorspace, 1, file);

	/* Write the pixel data. */
	qoi_error = encode(self, file);
	if (qoi_error != QOI_ERROR_NONE) {
		fclose(file);
		return -1;
	}

	/* Write the trailer. */
	char trailer[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
	fwrite(trailer, 1, 8, file);

	fclose(file);
	return 0;
#undef SAFEWRITE
}

/**
 * Gets the image buffer. Each pixel is represented with either 24 or 32 bits,
 * depending on if the Qoi object is set to QOI_CHANNEL_RGB or QOI_CHANNEL_RGBA
 * respectively. Keep in mind that this is the actual image buffer, and changing
 * values in this will result in the image being changed. Do not free this
 * buffer.
 */
uint8_t *qoi_get_raster(
		const Qoi *self)
{
	return self->data;
}

/**
 * Gets a copy of the image buffer. Each pixel is represented with either 32 or
 * 24 bits, depending on if the Qoi image has an alpha channel or not,
 * respectively. This buffer may be changed without changing the Qoi image
 * itself. The returned value should be freed using free() when no longer
 * needed.
 */
uint8_t *qoi_get_raster_clone(
		const Qoi *self)
{
	size_t size = self->width * self->height * self->channels;
	uint8_t *clone = malloc(size);
	memcpy(clone, self->data, size);
	return clone;
}

/**
 * Returns the error code for the previous failure. A string representation
 * of this code can be acquired via qoi_strerror().
 */
int qoi_errno()
{
	return qoi_error;
}

/**
 * Returns a string representation of the given error code. The returned string
 * is statically allocated and should NOT be freed.
 */
const char *qoi_strerror(
		int error_code)
{
	if (error_code < 0 || error_code >= QOI_INVALID_ERROR_CODE) {
		return qoi_strerror_messages[QOI_INVALID_ERROR_CODE];
	}

	return qoi_strerror_messages[error_code];
}

/**
 * Returns 1 if the Qoi image has an alpha channel, and 0 if it does not.
 */
int qoi_has_alpha(
		const Qoi *self)
{
	return self->channels == QOI_CHANNEL_RGBA;
}

/**
 * Returns the width (in pixels) of this image.
 */
int qoi_get_width(
		const Qoi *self)
{
	return self->width;
}

/**
 * Returns the height (in pixels) of this image.
 */
int qoi_get_height(
		const Qoi *self)
{
	return self->height;
}

/**
 * Returns the number of bytes between the start of subsequent pixel rows, in
 * the raster. This will always be equal to the width times 3 or 4 (3 if an
 * alpha channel does not exist, and 4 if it does).
 */
int qoi_get_rowstride(
		const Qoi *self)
{
	return self->width * self->channels;
}

/**
 * Converts the given four bytes into a 32 bit number by interpreting the bytes
 * as big endian.
 */
static uint32_t big_endian(
		const unsigned char *raw)
{
	return (raw[0] << 24) | (raw[1] << 16) | (raw[2] << 8)  | raw[3];
}

/**
 * Stores the 32 bit numerical INPUT into a big endian array OUTPUT.
 */
static void big_endian_r(
		char *output,
		const uint32_t input)
{
	output[0] = (input & 0xFF000000) >> 24;
	output[1] = (input & 0xFF0000) >> 16;
	output[2] = (input & 0xFF00) >> 8;
	output[3] = input & 0xFF;
}

/**
 * Retrieves the size in bytes of the file given by the FILEPATH.
 */
static ssize_t filesize(
		const char *filepath)
{
	struct stat meta;
	if (stat(filepath, &meta) == -1) {
		return -1;
	}

	return meta.st_size;
}

/**
 * Decrypts the raw file data INPUT into a pixel buffer OUTPUT.
 */
static int decode(
		const unsigned char *input,
		char *output,
		const size_t output_size,
		const char has_alpha_channel)
{
	int output_index = 0, input_index = 0;

	color last_color = { .r = 0, .g = 0, .b = 0, .a = 255 };
	color previous_colors[64];
	memset(previous_colors, 0, sizeof(previous_colors));

	while (output_index < output_size) {
		uint8_t byte = input[input_index++];

		if (IS_QOI_OP_RGB(byte)) {
			output[output_index]     = input[input_index++];
			output[output_index + 1] = input[input_index++];
			output[output_index + 2] = input[input_index++];
			if (has_alpha_channel) {
				output[output_index + 3] = last_color.a;
			}
		} else if (IS_QOI_OP_RGBA(byte)) {
			output[output_index]     = input[input_index++];
			output[output_index + 1] = input[input_index++];
			output[output_index + 2] = input[input_index++];
			if (has_alpha_channel) {
				output[output_index + 3] = input[input_index++];
			}
		} else if (IS_QOI_OP_INDEX(byte)) {
			int index = byte & 0x3F;
			last_color = previous_colors[index];
			output[output_index]     = last_color.r;
			output[output_index + 1] = last_color.g;
			output[output_index + 2] = last_color.b;
			if (has_alpha_channel) {
				output[output_index + 3] = last_color.a;
			}
		} else if (IS_QOI_OP_DIFF(byte)) {
			uint8_t dr = (byte & 0x30) >> 4;
			uint8_t dg = (byte & 0x0C) >> 2;
			uint8_t db = byte & 0x03;

			uint8_t r = last_color.r + (dr - 2);
			uint8_t g = last_color.g + (dg - 2);
			uint8_t b = last_color.b + (db - 2);

			output[output_index]     = r;
			output[output_index + 1] = g;
			output[output_index + 2] = b;
			if (has_alpha_channel) {
				output[output_index + 3] = last_color.a;
			}
		} else if (IS_QOI_OP_LUMA(byte)) {
			int8_t dg = (byte & 0x3F) - 32;
			int8_t drdg = (input[input_index] & 0xF0) >> 4;
			int8_t dbdg = input[input_index] & 0x0F;
			input_index++;

			uint8_t r = last_color.r + dg + (drdg - 8);
			uint8_t g = last_color.g + dg;
			uint8_t b = last_color.b + dg + (dbdg - 8);

			output[output_index]     = r;
			output[output_index + 1] = g;
			output[output_index + 2] = b;
			if (has_alpha_channel) {
				output[output_index + 3] = last_color.a;
			}
		} else if (IS_QOI_OP_RUN(byte)) {
			int8_t run_length = byte & 0x3F;
			for (int i = 0; i < run_length + 1; i++) {
				output[output_index++] = last_color.r;
				output[output_index++] = last_color.g;
				output[output_index++] = last_color.b;
				if (has_alpha_channel) {
					output[output_index++] = last_color.a;
				}
			}
			output_index -= 3 + has_alpha_channel;
		}

		last_color.r = output[output_index];
		last_color.g = output[output_index + 1];
		last_color.b = output[output_index + 2];
		if (has_alpha_channel) {
			last_color.a = output[output_index + 3];
		}
		output_index += 3 + has_alpha_channel;

		previous_colors[color_hash(last_color)] = last_color;
	}

	return 0;
}

/**
 * Encodes the pixel data in SELF and writes it into the FILE. The FILE should
 * already be open, and will not be closed by this function. Returns 0 on
 * success and a qoi_error code on failure. This does not write the header nor
 * the trailer.
 */
static int encode(
		const Qoi *self,
		FILE *file)
{
	int pixels = self->width * self->height;

	color last_color = { .r=0, .g=0, .b=0, .a=255 };
	color previous_colors[64];
	memset(previous_colors, 0, sizeof(previous_colors));

	for (int i = 0; i < pixels; i++) {
		/* Determine the color of the next pixel to process. */
		color current_pixel = create_color(
				self->data + (i * self->channels),
				self->channels);

		/* Determine the differences in colors, used to figure out which
		 * operation to use to encode the data. */
		int8_t dr = current_pixel.r - last_color.r;
		int8_t dg = current_pixel.g - last_color.g;
		int8_t db = current_pixel.b - last_color.b;
		int8_t da = current_pixel.a - last_color.a;

		int8_t drdg = dr - dg;
		int8_t dbdg = db - dg;

		int idx;

		if (color_equal(last_color, current_pixel)) {
			/* Case 1: Use a run of the previous color. */
			int length = 1;
			while (i + length < pixels &&
			       color_equal(last_color,
			                   create_color(self->data + ((i + length) * self->channels),
			                                self->channels))) {

				++length;
			}

			/* Write a run-length operation. */
			length = MIN(length - 1, 61);
			uint8_t write = 0xC0 | length;
			if (fwrite(&write, 1, 1, file) < 1) {
				return QOI_ERROR_DISK_SPACE;
			}

			i += length;
		} else if (dr >= -2 && dr <= 1 &&
		           dg >= -2 && dg <= 1 &&
		           db >= -2 && db <= 1 &&
		           da == 0) {

			/* Case 2: Use a difference of each of red, green, and blue. */
			uint8_t write = 0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2);
			if (fwrite(&write, 1, 1, file) < 1) {
				return QOI_ERROR_DISK_SPACE;
			}
		} else if ((idx = index_of(previous_colors, 64, current_pixel)) != -1) {
			/* Case 3: Use an index in the previous colors array. */
			if (fwrite(&idx, 1, 1, file) < 1) {
				return QOI_ERROR_DISK_SPACE;
			}
		} else if (dg >= -32 && dg <= 31 &&
		           drdg >= -8 && drdg <= 7 &&
				   dbdg >= -8 && dbdg <= 7 &&
				   da == 0) {

			/* Case 4: Use a change in luma. */
			uint8_t write[2];
			write[0] = 0x80 | (dg + 32);
			write[1] = ((drdg + 8) << 4) | (dbdg + 8);
			if (fwrite(write, 1, 2, file) < 2) {
				return QOI_ERROR_DISK_SPACE;
			}
		} else if (da == 0) {
			/* Case 5: Completely redefine the red, green, and blue values. */
			uint8_t write[4];
			write[0] = 0xFE;
			write[1] = current_pixel.r;
			write[2] = current_pixel.g;
			write[3] = current_pixel.b;

			if (fwrite(write, 1, 4, file) < 4) {
				return QOI_ERROR_DISK_SPACE;
			}
		} else {
			/* Case 6: Completely redefine r, g, b, and alpha values. */
			uint8_t write[5];
			write[0] = 0xFF;
			write[1] = current_pixel.r;
			write[2] = current_pixel.g;
			write[3] = current_pixel.b;
			write[4] = current_pixel.a;

			if (fwrite(write, 1, 5, file) < 5) {
				return QOI_ERROR_DISK_SPACE;
			}
		}
		
		last_color = current_pixel;
		previous_colors[color_hash(current_pixel)] = current_pixel;
	}

	return QOI_ERROR_NONE;
}

/**
 * Returns the index in the ARRAY that contains the given VALUE.
 */
static int index_of(
		const color *array,
		const size_t arraylen,
		const color value)
{
	for (int i = 0; i < arraylen; i++) {
		if (color_equal(array[i], value)) {
			return i;
		}
	}

	return -1;
}

/**
 * Returns the number of channels present in the image. This is either 3 or 4,
 * depending on if the image has an alpha channel or not.
 */
int qoi_get_channels(
		const Qoi *self)
{
	return self->channels;
}
