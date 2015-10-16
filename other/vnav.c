// gcc -std=c99 -g vnav.c iio.o -o vnav -lX11 -ltiff -lpng
//
// A program for visualizing borehole images of size 361xN, where N is huge.
//
// It displays the dip-picker transform on a side window, to aid the picking.
//
//
//
// TODO:
//
// 1. Use always the same buffers for the strip and the transform, instead
// of allocating anew at each computation.  This will allow to recover them if
// nothing has changed (e.g., for updating an overlay).
//
// 2. Add display of the max position (overlaid on the transform space)
//
// 3. Add display of the corresponding sinusoids as you hover through the
// transform space.
//
// 4. Put all the parameters explicitly inside the state structure.
//
// 5. Add keyboard actions to change the values of these parameters.
//
// 6. Inversive parametrization (and other altenatives maybe).
//
// 7. ``Look inside the well'' warping
//
// 8. Detection by randomized sampling (possibly coupled to the idle process).
//
// 9. Print the numbers of current DEPTH, A, B, DIP, Azimuth
//
// 10. Several options for automatic local contrast changes on both sides
//
// 11. Zoom-in and out of the Hough space, with the mouse-wheel
//
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <stdint.h>
#include "iio.h" // (for debug only)

#define TIFFU_OMIT_MAIN
#include "tiffu.c"

#ifndef FTR_BACKEND
#define FTR_BACKEND 'x'
#endif
#include "ftr.c"

#define WHEEL_FACTOR 1.4

// data structure for the image viewer
// this data goes into the "userdata" field of the FTR window structure
struct pan_state {
	// 1. inpyt image data
	int w, h;
	struct tiff_octaves t[1];

	// 2. view port parameters
	int octave;
	double zoom_x, zoom_y, offset_x, offset_y;
	double a, b;

	// 3. buffers for both windows
	bool has_hough;
	int strip_w, strip_h;
	int hough_w, hough_h;
	float *strip, *hough;

	// 4. transform parameters
	double aradius, min_grad;
	double pre_blur_sigma, post_blur_sigma;
	char *pre_blur_type, *post_blur_type;
};

// change of coordinates: from window "int" pixels to image "double" point
static void window_to_image(double p[2], struct pan_state *e, int i, int j)
{
	p[0] = e->offset_x + i / e->zoom_x;
	p[1] = e->offset_y + j / e->zoom_y;
}

// change of coordinates: from image "double" point to window "int" pixel
static void image_to_window(int i[2], struct pan_state *e, double x, double y)
{
	i[0] = floor(x * e->zoom_x - e->offset_x);
	i[1] = floor(y * e->zoom_y - e->offset_y);
}

static float getsample_0(float *x, int w, int h, int i, int j, int l)
{
	if (i < 0 || i >= w) return 0;
	if (j < 0 || j >= h) return 0;
	return x[3*(j*w+i)+l];
}

static void interpolate_at(float *out, float *x, int w, int h, float p, float q)
{
	out[0] = getsample_0(x, w, h, (int)p, (int)q, 0);
	out[1] = getsample_0(x, w, h, (int)p, (int)q, 1);
	out[2] = getsample_0(x, w, h, (int)p, (int)q, 2);
}

static int mod(int n, int p)
{
	if (p < 1)
		fail("bad modulus %d\n", p);

	int r;
	if (n >= 0)
		r = n % p;
	else {
		r = p - (-n) % p;
		if (r == p)
			r = 0;
	}
	if (r < 0 || r >= p)
		fprintf(stderr, "mod(%d,%d)=%d\n",n,p,r);
	assert(r >= 0);
	assert(r < p);
	return r;
}

// evaluate the value a position (p,q) in image coordinates
static float pixel(struct pan_state *e, double p, double q)
{
	//if (p < 0 || q < 0 || p > e->t->i->w-1 || q > e->t->i->h-1) {
	//	int ip = p-256;
	//	int iq = q-256;
	//	int pip = mod(ip/256, 2);
	//	int piq = mod(iq/256, 2);
	//	int val = mod(pip+piq,2);
	//	out[0] = out[1] = out[2] = 127+val*64;
	//	return;
	//}
	if (p < 0 || q < 0 || p > e->t->i->w-1 || q > e->t->i->h-1) {
		p = mod(p, e->t->i->w);
		if (q < 0 || q >= e->t->i->h)
			return NAN;
		//q = mod(q, e->t->i->h);
	}

	int fmt = e->t->i->fmt;
	int bps = e->t->i->bps;
	int spp = e->t->i->spp;
	int ss = bps / 8;

	double factorx = 1.0/e->zoom_x;
	double factory = 1.0/e->zoom_y;
	int o = e->octave;
	if (o < 0) { o = 0; factorx = 1; factory = 1; }
	char *pix = tiff_octaves_getpixel(e->t, o, p/factorx, q/factory);

	if (spp > 1)
		fail("do not support pd = %d\n", spp);

	return from_sample_to_double(pix, fmt, bps);
}

static void action_print_value_under_cursor(struct FTR *f, int x, int y)
{
	if (x<f->w && x>=0 && y<f->h && y>=0) {
		struct pan_state *e = f->userdata;
		double p[2];
		window_to_image(p, e, x, y);
		float v = pixel(e, p[0], p[1]);
		fprintf(stderr, "%g %g, value %g\n",p[0],p[1],v);
		//float c[3];
		//interpolate_at(c, e->frgb, e->w, e->h, p[0], p[1]);
		//printf("%g\t%g\t: %g\t%g\t%g\n", p[0], p[1], c[0], c[1], c[2]);
	}
}


#include<stdarg.h>
static void img_debug(float *x, int w, int h, int pd, const char *fmt, ...)
{
	//return;
	va_list ap;
	char fname[FILENAME_MAX];
	va_start(ap, fmt);
	vsnprintf(fname, FILENAME_MAX, fmt, ap);
	va_end(ap);
	fprintf(stderr, "IMG_DEBUG(%dx%d,%d) \"%s\"\n", w, h, pd, fname);
	iio_save_image_float_vec(fname, x, w, h, pd);
}

static void action_offset_viewport(struct FTR *f, int dx, int dy)
{
	struct pan_state *e = f->userdata;
	e->offset_x -= dx/e->zoom_x;
	e->offset_y -= dy/e->zoom_y;

	e->has_hough = false;
	f->changed = 1;
}

static void action_reset_zoom_and_position(struct FTR *f)
{
	struct pan_state *e = f->userdata;

	e->zoom_x = 1;
	e->zoom_y = 1;
	e->octave = 0;
	e->offset_x = 0;
	e->offset_y = 0;
	/*
	e->a = 1;
	e->b = 0;
	*/

	e->has_hough = false;
	f->changed = 1;
}

static void action_contrast_change(struct FTR *f, float afac, float bshift)
{
	struct pan_state *e = f->userdata;

	e->a *= afac;
	e->b += bshift;

	f->changed = 1;
}

static void action_qauto(struct FTR *f)
{
	struct pan_state *e = f->userdata;

	//float m = INFINITY, M = -m;
	float m = 0, M = 255;
	//int pid = 3;
	//for (int i = 0; i < 3 * e->pyr_w[pid] * e->pyr_h[pid]; i++)
	//{
	//	float g = e->pyr_rgb[pid][i];
	//	m = fmin(m, g);
	//	M = fmax(M, g);
	//}

	e->a = 255 / ( M - m );
	e->b = 255 * m / ( m - M );

	f->changed = 1;
}

static void action_center_contrast_at_point(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	double p[2];
	window_to_image(p, e, x, y);
	float C = pixel(e, p[0], p[1]);
	if (!isfinite(C)) C = 0;

	e->b = 127.5 - e->a * C;

	f->changed = 1;
}

static void action_base_contrast_at_point(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	double p[2];
	window_to_image(p, e, x, y);
	float C = pixel(e, p[0], p[1]);
	if (!isfinite(C)) C = 0;

	e->b =  255 - e->a * C;

	f->changed = 1;
}

static void action_contrast_span(struct FTR *f, float factor)
{
	struct pan_state *e = f->userdata;

	float c = (127.5 - e->b)/ e->a;
	e->a *= factor;
	e->b = 127.5 - e->a * c;

	f->changed = 1;
}

static void action_change_zoom_to_factor(struct FTR *f, int x, int y,
		double Fx, double Fy)
{
	struct pan_state *e = f->userdata;

	if (Fx == 1 && Fy == 1) e->octave = 0;

	double c[2];
	window_to_image(c, e, x, y);

	e->zoom_x = 1/Fx;
	e->zoom_y = 1/Fy;
	e->offset_x = c[0] - x/e->zoom_x;
	e->offset_y = c[1] - y/e->zoom_y;
	fprintf(stderr, "\t zoom changed to %g %g {%g %g}\n", e->zoom_x, e->zoom_y, e->offset_x, e->offset_y);

	e->has_hough = false;
	f->changed = 1;
}

#include "simpois.c"

#define OMIT_BLUR_MAIN
#include "blur.c"

#define OMIT_MAIN_TDIP
#include "tdip.c"


static void action_compute_hough(struct FTR *f)
{
	fprintf(stderr, "compute hough\n");
	struct pan_state *e = f->userdata;

	// buffer for the image data (current strip)
	int w = e->strip_w;//STRIP_WIDTH;
	int h = f->h;
	float *strip = e->strip;
	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	{
		double p[2];
		window_to_image(p, e, i, j);
		float v = pixel(e, p[0], p[1]);
		if (v > 2000) v = NAN;
		strip[j*w+i] = v;
	}
	img_debug(strip, w, h, 1, "/tmp/strip_at_z%g_x%g_y%g.tiff",
			e->zoom_y, e->offset_x, e->offset_y);

	// buffer for the transform data
	int tside = e->hough_w;
	float *hough = e->hough;
	for (int i = 0; i < tside * tside; i++)
		hough[i] = 0;

	// inpaint the "NAN" values of the image data
	poisson_recursive(strip, strip, 0, w, h, 0.4, 10, 99);
	img_debug(strip, w, h, 1, "/tmp/istrip_at_z%g_x%g_y%g.tiff",
			e->zoom_y, e->offset_x, e->offset_y);

	// blur the image
	float sigma[1] = {e->pre_blur_sigma};
	blur_2d(strip, strip, w, h, 1, e->pre_blur_type, sigma, 1);
	img_debug(strip, w, h, 1, "/tmp/bistrip_at_z%g_x%g_y%g.tiff",
			e->zoom_y, e->offset_x, e->offset_y);

	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	{
		double p[2];
		window_to_image(p, e, i, j);
		float v = pixel(e, p[0], p[1]);
		if (!isfinite(v) || v > 2000)
			strip[j*w+i] = NAN;
	}

	// compute the dip picker transform
	tdip(hough, e->aradius, tside, strip, w, h);
	img_debug(hough, tside, tside, 1, "/tmp/hbistrip_at_z%g_x%g_y%g.tiff",
			e->zoom_y, e->offset_x, e->offset_y);

	// blur the accumulator
	sigma[0] = e->post_blur_sigma;
	blur_2d(hough, hough, tside, tside, 1, e->post_blur_type, sigma, 1);

	//// compute statistics
	//float hmax = -INFINITY;
	//int xmax, ymax;
	//for (int j = 0; j < tside; j++)
	//for (int i = 0; i < tside; i++)
	//{
	//	int ij = i + j * tside;
	//	if (hough[ij] > hmax) {
	//		hmax = hough[ij];
	//		xmax = i;
	//		ymax = j;
	//	}
	//}

	//// dump the blurred image to the window
	//assert(f->w == tside + w);
	//assert(f->h == h);
	//for (int j = 0; j < w; j++)
	//for (int i = 0; i < h; i++)
	//for (int l = 0; l < 3; l++)
	//	f->rgb[3*(i+j*f->w)+l] = strip[i+j*w];

	//// dump the transform to the window
	//assert(f->w == tside + w);
	//assert(f->h == tside);
	//for (int j = 0; j < tside; j++)
	//for (int i = 0; i < tside; i++)
	//for (int l = 0; l < 3; l++)
	//	f->rgb[3*(i+w+j*f->w)+l] = 255 * hough[i+j*tside] / hmax;

	e->has_hough = true;
	f->changed = 1;
}



//static void action_increase_zoom(struct FTR *f, int x, int y)
//{
//	action_change_zoom_by_factor(f, x, y, WHEEL_FACTOR);
//}
//
//static void action_decrease_zoom(struct FTR *f, int x, int y)
//{
//	action_change_zoom_by_factor(f, x, y, 1.0/WHEEL_FACTOR);
//}


static void action_increase_octave(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	if (e->octave < e->t->noctaves - 1) {
		e->octave += 1;
		double fac = 1 << e->octave;
		if (e->octave < 0) fac = 1.0/(1<<-e->octave);
		action_change_zoom_to_factor(f, x, y, 1, fac);
	}

	fprintf(stderr, "increased octave to %d\n", e->octave);
}

static void action_decrease_octave(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	if (e->octave > 0) {
		e->octave -= 1;
		double fac = 1 << e->octave;
		action_change_zoom_to_factor(f, x, y, 1, fac);
	}
	else if (e->octave <= 0) {
		e->octave -= 1;
		double fac = 1.0/(1 << -e->octave);
		action_change_zoom_to_factor(f, x, y, 1, fac);
	}

	fprintf(stderr, "decreased octave to %d\n", e->octave);
}

static unsigned char float_to_byte(float x)
{
	if (x < 0) return 0;
	if (x > 255) return 255;
	// set gamma=2
	//float r = x * x / 255;
	//
	//float n = x / 255;
	//float r = (n*n)*n;
	//return r*255;
	return x;
}

// dump the image acording to the state of the viewport
static void pan_exposer(struct FTR *f, int b, int m, int x, int y)
{
	struct pan_state *e = f->userdata;

	// for every pixel in the window
	for (int j = 0; j < f->h; j++)
	for (int i = 0; i < e->strip_w; i++)
	{
		float C = e->strip[e->strip_w * j + i];
		if (!e->has_hough) {
			// compute the position of this pixel in the image
			double p[2];
			window_to_image(p, e, i, j);

			// evaluate the color value of the image at this position
			C = pixel(e, p[0], p[1]);
		}

		// transform the value into RGB using the contrast change (a,b)
		float v = 0;
		if (isfinite(C))
			v = e->a * C + e->b;
		unsigned char *dest = f->rgb + 3 * (j * f->w + i);
		for (int l = 0; l < 3; l++)
			dest[l] = float_to_byte(v);
	}

	// draw grid lines on the hough space
	assert(e->hough_w == f->h);
	assert(e->hough_w + e->strip_w == f->w);
	if (e->has_hough) {
	//assert(f->w == tside + w);
	//assert(f->h == tside);

		int tside = e->hough_w;
		// compute statistics
		float hmax = -INFINITY;
		int xmax, ymax;
		for (int j = 0; j < tside; j++)
		for (int i = 0; i < tside; i++)
		{
			int ij = i + j * tside;
			if (e->hough[ij] > hmax) {
				hmax = e->hough[ij];
				xmax = i;
				ymax = j;
			}
		}
		for (int j = 0; j < e->hough_w; j++)
		for (int i = 0; i < e->hough_w; i++)
		for (int l = 0; l < 3; l++)
			f->rgb[3*(i+e->strip_w+j*f->w)+l] = 255 * e->hough[i+j*e->hough_w] / hmax;
		for (int dj = -20; dj <= 20; dj++)
		for (int di = -20; di <= 20; di++)
		{
			int ii = xmax + di;
			int jj = ymax + dj;
			if (insideP(e->hough_w, e->hough_h, ii, jj) &&  hypot(di, dj) < 20) {
				f->rgb[3*(ii+e->strip_w+jj*f->w)+0] *= 2;
				f->rgb[3*(ii+e->strip_w+jj*f->w)+2] /= 2;
			}
		}
		f->rgb[3*(xmax+e->strip_w+ymax*f->w)+0] = 255;
		f->rgb[3*(xmax+e->strip_w+ymax*f->w)+1] = 0;
		f->rgb[3*(xmax+e->strip_w+ymax*f->w)+2] = 0;
	}
	for (int i = 0; i < e->hough_w; i++)
	{
		f->rgb[3*(f->w*i + e->strip_w + e->hough_w/2)+1] /= 2;
		f->rgb[3*(f->w*e->hough_w/2 + e->strip_w + i)+1] /= 2;
		f->rgb[3*(f->w*i + e->strip_w + e->hough_w/2)+2] = 255;
		f->rgb[3*(f->w*e->hough_w/2 + e->strip_w + i)+2] = 255;
	}

	f->changed = 1;
}

// update offset variables by dragging
static void pan_motion_handler(struct FTR *f, int b, int m, int x, int y)
{
	struct pan_state *e = f->userdata;

	static double ox = 0, oy = 0;

	if (m == FTR_BUTTON_LEFT)   action_offset_viewport(f, x - ox, y - oy);
	if (m == FTR_BUTTON_MIDDLE) action_print_value_under_cursor(f, x, y);
	if (m == FTR_MASK_SHIFT)    action_center_contrast_at_point(f, x, y);
	if (m == FTR_MASK_CONTROL)  action_base_contrast_at_point(f, x, y);

	ox = x;
	oy = y;
}

static void pan_button_handler(struct FTR *f, int b, int m, int x, int y)
{
	struct pan_state *e = f->userdata;

	//fprintf(stderr, "button b=%d m=%d\n", b, m);
	if (b == FTR_BUTTON_UP && (m==FTR_MASK_SHIFT || m==FTR_MASK_CONTROL)) {
		action_contrast_span(f, 1/1.3); return; }
	if (b == FTR_BUTTON_DOWN && ((m==FTR_MASK_SHIFT)||m==FTR_MASK_CONTROL)){
		action_contrast_span(f, 1.3); return; }
	//if (b == FTR_BUTTON_RIGHT && m == FTR_MASK_CONTROL) {
	//	action_reset_zoom_only(f, x, y); return; }
	if (b == FTR_BUTTON_MIDDLE) action_print_value_under_cursor(f, x, y);
	if (b == FTR_BUTTON_DOWN)   action_increase_octave(f, x, y);
	if (b == FTR_BUTTON_UP  )   action_decrease_octave(f, x, y);
	if (b == FTR_BUTTON_RIGHT)  action_reset_zoom_and_position(f);
}

void key_handler_print(struct FTR *f, int k, int m, int x, int y)
{
	fprintf(stderr, "key pressed %d '%c' (%d) at %d %d\n",
			k, isalpha(k)?k:' ', m, x, y);
}

void pan_key_handler(struct FTR *f, int k, int m, int x, int y)
{
	fprintf(stderr, "PAN_KEY_HANDLER  %d '%c' (%d) at %d %d\n",
			k, isalpha(k)?k:' ', m, x, y);

	if (k == '+'||k=='=') action_decrease_octave(f, f->w/2, f->h/2);
	if (k == '-') action_increase_octave(f, f->w/2, f->h/2);
	if (k == '1') action_change_zoom_to_factor(f, x, y, 1, 1);

	if (k == 'h') action_compute_hough(f);

	//if (k == 'p') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1.1);
	//if (k == 'm') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1/1.1);
	//if (k == 'P') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1.006);
	//if (k == 'M') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1/1.006);

	//if (k == 'a') action_contrast_change(f, 1.3, 0);
	//if (k == 'A') action_contrast_change(f, 1/1.3, 0);
	//if (k == 'b') action_contrast_change(f, 1, 1);
	//if (k == 'B') action_contrast_change(f, 1, -1);
	if (k == 'n') action_qauto(f);

	// if ESC or q, exit
	if  (k == '\033' || k == 'q')
		ftr_notify_the_desire_to_stop_this_loop(f, 1);

	// arrows move the viewport
	if (k > 1000) {
		int d[2] = {0, 0};
		int inc = -10;
		if (m & FTR_MASK_SHIFT  ) inc /= 10;
		if (m & FTR_MASK_CONTROL) inc *= 10;
		switch (k) {
		case FTR_KEY_LEFT : d[0] -= inc; break;
		case FTR_KEY_RIGHT: d[0] += inc; break;
		case FTR_KEY_UP   : d[1] -= inc; break;
		case FTR_KEY_DOWN : d[1] += inc; break;
		}
		if (k == FTR_KEY_PAGE_UP)   d[1] = +f->h/3;
		if (k == FTR_KEY_PAGE_DOWN) d[1] = -f->h/3;
		action_offset_viewport(f, d[0], d[1]);
	}
}

// image file input/output (wrapper around iio) {{{1
#include <stdint.h>
#include "iio.h"
static unsigned char *read_image_uint8_rgb(char *fname, int *w, int *h)
{
	int pd;
	unsigned char *x = iio_read_image_uint8_vec(fname, w, h, &pd);
	if (pd == 3) return x;
	unsigned char *y = malloc(3**w**h);
	for (int i = 0; i < *w**h; i++) {
		switch(pd) {
		case 1:
			y[3*i+0] = y[3*i+1] = y[3*i+2] = x[i];
			break;
		case 2:
			y[3*i+0] = x[2*i+0];
			y[3*i+1] = y[3*i+2] = x[2*i+1];
			break;
		default:
			assert(pd > 3);
			for (int l = 0; l < 3; l++)
				y[3*i+l] = x[pd*i+l];
			break;
		}
	}
	free(x);
	return y;
}


#define STRIP_WIDTH 361
#define HOUGH_SIDE 512
int main_pan(int c, char *v[])
{
	TIFFSetWarningHandler(NULL);//suppress warnings

	// process input arguments
	if (c != 2) {
		fprintf(stderr, "usage:\n\t%s pyrpattern\n", *v);
		//                          0 1
		return 1;
	}
	char *pyrpattern = v[1];

	// read image
	struct pan_state e[1];
	int megabytes = 100;
	tiff_octaves_init(e->t, pyrpattern, megabytes);
	if (e->t->i->w != STRIP_WIDTH)
		fail("expected an image of width %d (got %d)\n",
						e->t->i->w, STRIP_WIDTH);

	// init state
	e->a = 1;
	e->b = 0;
	e->strip_w = STRIP_WIDTH;
	e->strip_h = HOUGH_SIDE;
	e->hough_w = HOUGH_SIDE;
	e->hough_h = HOUGH_SIDE; // (unused)
	e->strip = xmalloc(e->strip_w * e->strip_h * sizeof*e->strip);
	e->hough = xmalloc(e->hough_w * e->hough_w * sizeof*e->hough);
	e->has_hough = false;

	e->aradius = 1.5;
	e->pre_blur_sigma = 1;
	e->pre_blur_type = "gaussian";
	e->post_blur_sigma = 1;
	e->post_blur_type = "cauchy";

	// open window
	struct FTR f = ftr_new_window(STRIP_WIDTH+HOUGH_SIDE, HOUGH_SIDE);
	f.userdata = e;
	action_reset_zoom_and_position(&f);
	ftr_set_handler(&f, "key"   , pan_key_handler);
	ftr_set_handler(&f, "button", pan_button_handler);
	ftr_set_handler(&f, "motion", pan_motion_handler);
	ftr_set_handler(&f, "expose", pan_exposer);
	int r = ftr_loop_run(&f);

	// cleanup and exit (optional)
	ftr_close(&f);
	return r;
}

int main(int c, char *v[])
{
	return main_pan(c, v);
}
