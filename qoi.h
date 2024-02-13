#ifndef QOI_H
#define QOI_H

#include <stdint.h>

/**
 * QOI supports two colorspaces:
 *   - sRGB with linear alpha, and
 *   - all channels linear.
 */
typedef uint8_t QoiColorspace;
extern const QoiColorspace QOI_COLORSPACE_SRGB;
extern const QoiColorspace QOI_COLORSPACE_LINEAR;

/**
 * QOI supports two channel widths: RGB and RGBA.
 */
typedef uint8_t QoiChannel;
extern const QoiChannel QOI_CHANNEL_RGBA;
extern const QoiChannel QOI_CHANNEL_RGB;

/**
 * Contains the main QOI object that can be operated upon.
 */
typedef struct Qoi Qoi;

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
		QoiChannel channels);

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
		void (*freeing_function)(void*));

/**
 * Construct a new QOI object from a QOI file. If the file is not valid,
 * this returns NULL, and qoi_errno() can be used to find out why. The returned
 * object should be freed using qoi_free() when no longer needed.
 */
Qoi *qoi_new_from_file(
		const char *filepath);

/**
 * Releases the resources held by a QOI object. Any buffers held by external
 * resources after this call will be invalid.
 */
void qoi_free(
		Qoi *self);

/**
 * Saves a QOI object to a .qoi file. On success returns 0, otherwise returns
 * -1. qoi_errno() can be used to find out why a save operation failed.
 */
int qoi_save(
		const Qoi *self,
		const char *filepath);

/**
 * Gets the image buffer. Each pixel is represented with either 24 or 32 bits,
 * depending on if the Qoi object is set to QOI_CHANNEL_RGB or QOI_CHANNEL_RGBA
 * respectively. Keep in mind that this is the actual image buffer, and changing
 * values in this will result in the image being changed. Do not free this
 * buffer.
 */
uint8_t *qoi_get_raster(
		const Qoi *self);

/**
 * Gets a copy of the image buffer. Each pixel is represented with either 32 or
 * 24 bits, depending on if the Qoi image has an alpha channel or not,
 * respectively. This buffer may be changed without changing the Qoi image
 * itself. The returned value should be freed using free() when no longer
 * needed.
 */
uint8_t *qoi_get_raster_clone(
		const Qoi *self);

/**
 * Returns the error code for the previous failure. A string representation
 * of this code can be acquired via qoi_strerror().
 */
int qoi_errno();

/**
 * Returns a string representation of the given error code. The returned string
 * is statically allocated and should NOT be freed.
 */
const char *qoi_strerror(
		int error_code);

/**
 * Returns 1 if the Qoi image has an alpha channel, and 0 if it does not.
 */
int qoi_has_alpha(
		const Qoi *self);

/**
 * Returns the width (in pixels) of this image.
 */
int qoi_get_width(
		const Qoi *self);

/**
 * Returns the height (in pixels) of this image.
 */
int qoi_get_height(
		const Qoi *self);

/**
 * Returns the number of bytes between the start of subsequent pixel rows, in
 * the raster. This will always be equal to the width times 3 or 4 (3 if an
 * alpha channel does not exist, and 4 if it does).
 */
int qoi_get_rowstride(
		const Qoi *self);

/**
 * Returns the number of channels present in the image. This is either 3 or 4,
 * depending on if the image has an alpha channel or not.
 */
int qoi_get_channels(
		const Qoi *self);

#endif /* QOI_H */
