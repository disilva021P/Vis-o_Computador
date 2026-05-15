//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//           INSTITUTO POLITÉCNICO DO CÁVADO E DO AVE
//                          2022/2023
//             ENGENHARIA DE SISTEMAS INFORMÁTICOS
//                    VISÃO POR COMPUTADOR
//
//             [  DUARTE DUQUE - dduque@ipca.pt  ]
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "vc.h"


IVC* vc_image_new(int width, int height, int channels, int levels)
{
	IVC* image = (IVC*)malloc(sizeof(IVC));

	if (image == NULL) return NULL;
	if ((levels <= 0) || (levels > 255)) return NULL;

	image->width = width;
	image->height = height;
	image->channels = channels;
	image->levels = levels;
	image->bytesperline = image->width * image->channels;
	image->data = (unsigned char*)malloc(image->width * image->height * image->channels * sizeof(char));

	if (image->data == NULL)
	{
		return vc_image_free(image);
	}

	return image;
}


IVC* vc_image_free(IVC* image)
{
	if (image != NULL)
	{
		if (image->data != NULL)
		{
			free(image->data);
			image->data = NULL;
		}

		free(image);
		image = NULL;
	}

	return image;
}



char* netpbm_get_token(FILE* file, char* tok, int len)
{
	char* t;
	int c;

	for (;;)
	{
		while (isspace(c = getc(file)));
		if (c != '#') break;
		do c = getc(file);
		while ((c != '\n') && (c != EOF));
		if (c == EOF) break;
	}

	t = tok;

	if (c != EOF)
	{
		do
		{
			*t++ = c;
			c = getc(file);
		} while ((!isspace(c)) && (c != '#') && (c != EOF) && (t - tok < len - 1));

		if (c == '#') ungetc(c, file);
	}

	*t = 0;

	return tok;
}


long int unsigned_char_to_bit(unsigned char* datauchar, unsigned char* databit, int width, int height)
{
	int x, y;
	int countbits;
	long int pos, counttotalbytes;
	unsigned char* p = databit;

	*p = 0;
	countbits = 1;
	counttotalbytes = 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = width * y + x;

			if (countbits <= 8)
			{
				*p |= (datauchar[pos] == 0) << (8 - countbits);

				countbits++;
			}
			if ((countbits > 8) || (x == width - 1))
			{
				p++;
				*p = 0;
				countbits = 1;
				counttotalbytes++;
			}
		}
	}

	return counttotalbytes;
}


void bit_to_unsigned_char(unsigned char* databit, unsigned char* datauchar, int width, int height)
{
	int x, y;
	int countbits;
	long int pos;
	unsigned char* p = databit;

	countbits = 1;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = width * y + x;

			if (countbits <= 8)
			{
				datauchar[pos] = (*p & (1 << (8 - countbits))) ? 0 : 1;

				countbits++;
			}
			if ((countbits > 8) || (x == width - 1))
			{
				p++;
				countbits = 1;
			}
		}
	}
}


IVC* vc_read_image(char* filename)
{
	FILE* file = NULL;
	IVC* image = NULL;
	unsigned char* tmp;
	char tok[20];
	long int size, sizeofbinarydata;
	int width, height, channels;
	int levels = 255;
	int v;

	if ((file = fopen(filename, "rb")) != NULL)
	{
		netpbm_get_token(file, tok, sizeof(tok));

		if (strcmp(tok, "P4") == 0) { channels = 1; levels = 1; }	
		else if (strcmp(tok, "P5") == 0) channels = 1;				
		else if (strcmp(tok, "P6") == 0) channels = 3;				
		else
		{
#ifdef VC_DEBUG
			printf("ERROR -> vc_read_image():\n\tFile is not a valid PBM, PGM or PPM file.\n\tBad magic number!\n");
#endif

			fclose(file);
			return NULL;
		}

		if (levels == 1) 
		{
			if (sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &width) != 1 ||
				sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &height) != 1)
			{
#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tFile is not a valid PBM file.\n\tBad size!\n");
#endif

				fclose(file);
				return NULL;
			}

			image = vc_image_new(width, height, channels, levels);
			if (image == NULL) return NULL;

			sizeofbinarydata = (image->width / 8 + ((image->width % 8) ? 1 : 0)) * image->height;
			tmp = (unsigned char*)malloc(sizeofbinarydata);
			if (tmp == NULL) return 0;

#ifdef VC_DEBUG
			printf("\nchannels=%d w=%d h=%d levels=%d\n", image->channels, image->width, image->height, levels);
#endif

			if ((v = fread(tmp, sizeof(unsigned char), sizeofbinarydata, file)) != sizeofbinarydata)
			{
#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tPremature EOF on file.\n");
#endif

				vc_image_free(image);
				fclose(file);
				free(tmp);
				return NULL;
			}

			bit_to_unsigned_char(tmp, image->data, image->width, image->height);

			free(tmp);
		}
		else 
		{
			if (sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &width) != 1 ||
				sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &height) != 1 ||
				sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &levels) != 1 || levels <= 0 || levels > 255)
			{
#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tFile is not a valid PGM or PPM file.\n\tBad size!\n");
#endif

				fclose(file);
				return NULL;
			}

			image = vc_image_new(width, height, channels, levels);
			if (image == NULL) return NULL;

#ifdef VC_DEBUG
			printf("\nchannels=%d w=%d h=%d levels=%d\n", image->channels, image->width, image->height, levels);
#endif

			size = image->width * image->height * image->channels;

			if ((v = fread(image->data, sizeof(unsigned char), size, file)) != size)
			{
#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tPremature EOF on file.\n");
#endif

				vc_image_free(image);
				fclose(file);
				return NULL;
			}
		}

		fclose(file);
	}
	else
	{
#ifdef VC_DEBUG
		printf("ERROR -> vc_read_image():\n\tFile not found.\n");
#endif
	}

	return image;
}


int vc_write_image(char* filename, IVC* image)
{
	FILE* file = NULL;
	unsigned char* tmp;
	long int totalbytes, sizeofbinarydata;

	if (image == NULL) return 0;

	if ((file = fopen(filename, "wb")) != NULL)
	{
		if (image->levels == 1)
		{
			sizeofbinarydata = (image->width / 8 + ((image->width % 8) ? 1 : 0)) * image->height + 1;
			tmp = (unsigned char*)malloc(sizeofbinarydata);
			if (tmp == NULL) return 0;

			fprintf(file, "%s %d %d\n", "P4", image->width, image->height);

			totalbytes = unsigned_char_to_bit(image->data, tmp, image->width, image->height);
			printf("Total = %ld\n", totalbytes);
			if (fwrite(tmp, sizeof(unsigned char), totalbytes, file) != totalbytes)
			{
#ifdef VC_DEBUG
				fprintf(stderr, "ERROR -> vc_read_image():\n\tError writing PBM, PGM or PPM file.\n");
#endif

				fclose(file);
				free(tmp);
				return 0;
			}

			free(tmp);
		}
		else
		{
			fprintf(file, "%s %d %d 255\n", (image->channels == 1) ? "P5" : "P6", image->width, image->height);

			if (fwrite(image->data, image->bytesperline, image->height, file) != image->height)
			{
#ifdef VC_DEBUG
				fprintf(stderr, "ERROR -> vc_read_image():\n\tError writing PBM, PGM or PPM file.\n");
#endif

				fclose(file);
				return 0;
			}
		}

		fclose(file);

		return 1;
	}

	return 0;
}


int vc_gray_negative(IVC* srcdst)
{
	unsigned char* data = (unsigned char*)srcdst->data;
	int width = srcdst->width;
	int height = srcdst->height;
	int bytesperline = srcdst->width * srcdst->channels;
	int channels = srcdst->channels;
	int x, y;
	long int pos;

	if ((srcdst->width <= 0) || (srcdst->height <= 0) || (srcdst->data == NULL)) return 0;
	if (channels != 1) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = y * bytesperline + x * channels;

			data[pos] = 255 - data[pos];
		}
	}

	return 1;
}

int vc_rgb_to_gray(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	int bytesperline_src = src->width * src->channels;
	int channels_src = src->channels;
	unsigned char* datadst = (unsigned char*)dst->data;
	int bytesperline_dst = dst->width * dst->channels;
	int channels_dst = dst->channels;
	int width = src->width;
	int height = src->height;
	int x, y;
	long int pos_src, pos_dst;
	float rf, gf, bf;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 3) || (dst->channels != 1)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			rf = (float)datasrc[pos_src];
			gf = (float)datasrc[pos_src + 1];
			bf = (float)datasrc[pos_src + 2];
			datadst[pos_dst] = (unsigned char)(0.299f*rf + 0.587f*gf + 0.114f*bf);
		}
		
	}

	return 1;
}

int vc_rgb_to_hsv(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;

	int width = src->width;
	int height = src->height;
	int channels_src = src->channels;
	int channels_dst = dst->channels;

	int bytesperline_src = width * channels_src;
	int bytesperline_dst = width * channels_dst;

	int x, y;
	long int pos_src, pos_dst;

	float rf, gf, bf;
	float rgb_max, rgb_min;
	float h, s, v;
	float delta;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 3) || (dst->channels != 3)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			rf = datasrc[pos_src] / 255.0f;
			gf = datasrc[pos_src + 1] / 255.0f;
			bf = datasrc[pos_src + 2] / 255.0f;

			rgb_max = (rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf));
			rgb_min = (rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf));

			v = rgb_max;			
			delta = rgb_max - rgb_min;

			if (rgb_max == 0)
				s = 0;
			else
				s = delta / v;

			if (delta == 0)
			{
				h = 0;
			}
			else
			{
				if (rgb_max == rf)
				{
					if (gf >= bf)
						h = 60.0f * (gf - bf) / delta;
					else
						h = 360.0f + 60.0f * (gf - bf) / delta;
				}
				else if (rgb_max == gf)
				{
					h = 120.0f + 60.0f * (bf - rf) / delta;
				}
				else
				{
					h = 240.0f + 60.0f * (bf - rf) / delta;
				}
			}

			datadst[pos_dst] = (unsigned char)((h / 360.0f) * 255.0f);
			datadst[pos_dst + 1] = (unsigned char)(s * 255.0f);
			datadst[pos_dst + 2] = (unsigned char)(v * 255.0f);
		}
	}
	return 1;
}
int vc_bgr_to_hsv(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;

	int width = src->width;
	int height = src->height;
	int channels_src = src->channels;
	int channels_dst = dst->channels;

	int bytesperline_src = width * channels_src;
	int bytesperline_dst = width * channels_dst;

	int x, y;
	long int pos_src, pos_dst;

	float rf, gf, bf;
	float rgb_max, rgb_min;
	float h, s, v;
	float delta;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 3) || (dst->channels != 3)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			rf = datasrc[pos_src+2] / 255.0f;
			gf = datasrc[pos_src + 1] / 255.0f;
			bf = datasrc[pos_src] / 255.0f;

			rgb_max = (rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf));
			rgb_min = (rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf));

			v = rgb_max;			// Value
			delta = rgb_max - rgb_min;

			if (rgb_max == 0)
				s = 0;
			else
				s = delta / v;

			if (delta == 0)
			{
				h = 0;
			}
			else
			{
				if (rgb_max == rf)
				{
					if (gf >= bf)
						h = 60.0f * (gf - bf) / delta;
					else
						h = 360.0f + 60.0f * (gf - bf) / delta;
				}
				else if (rgb_max == gf)
				{
					h = 120.0f + 60.0f * (bf - rf) / delta;
				}
				else
				{
					h = 240.0f + 60.0f * (bf - rf) / delta;
				}
			}

			// Escalar para 0-255
			datadst[pos_dst] = (unsigned char)((h / 360.0f) * 255.0f);
			datadst[pos_dst + 1] = (unsigned char)(s * 255.0f);
			datadst[pos_dst + 2] = (unsigned char)(v * 255.0f);
		}
	}
	return 1;
}

int vc_hsv_segmentation(IVC* src, IVC* dst, int hmin_gimp, int hmax_gimp, int smin_gimp, int smax_gimp, int vmin_gimp, int vmax_gimp)
{
	int hmin = (int)((hmin_gimp / 360.0f) * 255);
	int hmax = (int)((hmax_gimp / 360.0f) * 255);

	int smin = (int)((smin_gimp / 100.0f) * 255);
	int smax = (int)((smax_gimp / 100.0f) * 255);

	int vmin = (int)((vmin_gimp / 100.0f) * 255);
	int vmax = (int)((vmax_gimp / 100.0f) * 255);

	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int width = src->width;
	int height = src->height;
	int channels = src->channels;
	int bytesperline = width * channels;
	int x, y;
	long int pos;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL))
		return 0;

	if (src->channels != 3 || dst->channels != 1)
		return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = y * bytesperline + x * channels;

			int h = datasrc[pos];       
			int s = datasrc[pos + 1];   
			int v = datasrc[pos + 2];   

			if ((h >= hmin && h <= hmax) &&
				(s >= smin && s <= smax) &&
				(v >= vmin && v <= vmax))
			{
				datadst[y * width + x] = 255;  
			}
			else
			{
				datadst[y * width + x] = 0;    
			}
		}
	}

	return 1;
}

int vc_scale_gray_to_color_palette(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data; 
	int bytesperline_src = src->width * src->channels;
	int channels_src = src->channels; 

	unsigned char* datadst = (unsigned char*)dst->data;
	int bytesperline_dst = dst->width * dst->channels;
	int channels_dst = dst->channels;

	int width = src->width;
	int height = src->height;
	int x, y;
	long int pos_src, pos_dst;

	unsigned char gray;
	unsigned char r, g, b;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 1) || (dst->channels != 3)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			gray = datasrc[pos_src];

			if (gray < 64) { r = 0; g = (gray * 4); b = 255; }
			else if (gray < 128) { r = 0; g = 255; b = 255 - (gray - 64) * 4; }
			else if (gray < 192) { r = (gray - 128) * 4; g = 255; b = 0; }
			else { r = 255; g = 255 - ((gray - 192) * 4); b = 0; }

			datadst[pos_dst] = r;
			datadst[pos_dst + 1] = g;
			datadst[pos_dst + 2] = b;
		}
	}

	return 1;
}

int vc_gray_to_binary(IVC* src, IVC* dst, int threshold)
{
	unsigned char* datasrc = (unsigned char*)src->data; 	
	unsigned char* datadst = (unsigned char*)dst->data;

	int bytesperline_src = src->width * src->channels;
	int bytesperline_dst = dst->width * dst->channels;

	int channels_src = src->channels;
	int channels_dst = dst->channels;

	int width = src->width;
	int height = src->height;

	int x, y;
	long int pos_src, pos_dst;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 1) || (dst->channels != 1)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			if (datasrc[pos_src] > threshold)
			{
				datadst[pos_dst] = 255;
			}
			else
			{
				datadst[pos_dst] = 0;
			}
		}
	}

	return 1;
}

int vc_gray_to_binary_global_mean(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;

	int width = src->width;
	int height = src->height;
	int channels = src->channels;

	int bytesperline = width * channels;

	int x, y;
	long int pos;

	long sum = 0;
	int threshold;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 1) || (dst->channels != 1)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = y * bytesperline + x * channels;
			sum += datasrc[pos];
		}
	}

	threshold = sum / (width * height);

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = y * bytesperline + x * channels;

			if (datasrc[pos] > threshold)
				datadst[pos] = 255;
			else
				datadst[pos] = 0;
		}
	}

	return 1;
}

int vc_gray_to_binary_midpoint(IVC* src, IVC* dst, int kernel)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;

	int width = src->width;
	int height = src->height;
	int channels = src->channels;

	int bytesperline = src->bytesperline;

	int x, y, kx, ky;
	int offset;
	int half = kernel / 2;
	int vmin, vmax;
	int threshold;

	if (src == NULL || dst == NULL) return 0;
	if (src->width != dst->width)  return 0;
	if (src->height != dst->height) return 0;
	if (src->channels != 1)         return 0;
	if (dst->channels != 1)         return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			vmin = 255;
			vmax = 0;

			for (ky = -half; ky <= half; ky++)
			{
				for (kx = -half; kx <= half; kx++)
				{
					int nx = x + kx;
					int ny = y + ky;

					if (nx < 0) nx = 0;
					if (ny < 0) ny = 0;
					if (nx >= width)  nx = width - 1;
					if (ny >= height) ny = height - 1;

					int pixel = datasrc[ny * bytesperline + nx * channels];

					if (pixel < vmin) vmin = pixel;
					if (pixel > vmax) vmax = pixel;
				}
			}

			threshold = (int)(0.5f * (vmin + vmax));

			offset = y * bytesperline + x * channels;

			if (datasrc[offset] > threshold)
				datadst[offset] = 255;
			else
				datadst[offset] = 0;
		}
	}

	return 1;
}

int vc_binary_dilate(IVC* src, IVC* dst, int kernel) {

	unsigned char* datasrc, * datadst;

	int x, y, kx, ky;
	int width, height;
	int offset = kernel / 2;

	if (src == NULL || dst == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;

	width = src->width;
	height = src->height;

	datasrc = src->data;
	datadst = dst->data;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int found = 0;


			for (ky = -offset; ky <= offset && !found; ky++)
			{
				for (kx = -offset; kx <= offset; kx++)
				{
					int xx = x + kx;
					int yy = y + ky;

					if (xx >= 0 && xx < width && yy >= 0 && yy < height)
					{
						if (datasrc[yy * width + xx] == 255)
						{
							found = 1;
							break;
						}
					}
				}
			}

			if (found)
				datadst[y * width + x] = 255;
			else
				datadst[y * width + x] = 0;
		}
	}

	return 1;
}

int vc_binary_erode(IVC* src, IVC* dst, int kernel)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int offset = kernel / 2;
	int x, y, kx, ky;
	int allWhite;

	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;

	memset(datadst, 0, bytesperline * height);

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			allWhite = 1;

			for (ky = -offset; ky <= offset && allWhite; ky++)
			{
				for (kx = -offset; kx <= offset && allWhite; kx++)
				{
					int nx = x + kx;
					int ny = y + ky;

					if (nx >= 0 && nx < width && ny >= 0 && ny < height)
					{
						// Se algum vizinho for preto, para imediatamente
						if (datasrc[ny * bytesperline + nx * channels] == 0)
						{
							allWhite = 0;
						}
					}
					else
					{
						allWhite = 0;
					}
				}
			}

			if (allWhite)
			{
				datadst[y * bytesperline + x * channels] = 255;
			}
		}
	}

	return 1;
}

int vc_gray_to_binary_interval(IVC* src, IVC* dst, int tmin, int tmax)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int x, y;

	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int pos = y * bytesperline + x * channels;
			int valor = datasrc[pos];

			if (valor >= tmin && valor <= tmax)
				datadst[pos] = 255;
			else
				datadst[pos] = 0;
		}
	}
	return 1;
}

int vc_apply_mask(IVC* src_gray, IVC* src_mask, IVC* dst)
{
	unsigned char* datagray = (unsigned char*)src_gray->data;
	unsigned char* datamask = (unsigned char*)src_mask->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int width = src_gray->width;
	int height = src_gray->height;
	int bytesperline = src_gray->bytesperline;
	int channels = src_gray->channels;
	int x, y;

	if (src_gray->width <= 0 || src_gray->height <= 0) return 0;
	if (src_gray->width != src_mask->width || src_gray->height != src_mask->height) return 0;
	if (src_gray->width != dst->width || src_gray->height != dst->height) return 0;
	if (src_gray->channels != 1 || src_mask->channels != 1 || dst->channels != 1) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int pos = y * bytesperline + x * channels;

			if (datamask[pos] == 255)
				datadst[pos] = datagray[pos];
			else
				datadst[pos] = 0;
		}
	}
	return 1;
}

OVC* vc_binary_blob_labelling(IVC* src, IVC* dst, int* nlabels)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int x, y, a, b;
	long int i;
	size_t size;
	long int posX, posA, posB, posC, posD;
	int labeltable[256] = { 0 };
	int labelarea[256] = { 0 };
	int label = 1; // Etiqueta inicial.
	int num, tmplabel;
	OVC* blobs; 
	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height) || (src->channels != dst->channels)) return NULL;
	if (channels != 1) return NULL;

	memcpy(datadst, datasrc, bytesperline * height);

	for (i = 0, size = bytesperline * height; i < size; i++)
	{
		if (datadst[i] != 0) datadst[i] = 255;
	}

	for (y = 0; y < height; y++)
	{
		datadst[y * bytesperline + 0 * channels] = 0;
		datadst[y * bytesperline + (width - 1) * channels] = 0;
	}
	for (x = 0; x < width; x++)
	{
		datadst[0 * bytesperline + x * channels] = 0;
		datadst[(height - 1) * bytesperline + x * channels] = 0;
	}

	for (y = 1; y < height - 1; y++)
	{
		for (x = 1; x < width - 1; x++)
		{

			posA = (y - 1) * bytesperline + (x - 1) * channels; // A
			posB = (y - 1) * bytesperline + x * channels; // B
			posC = (y - 1) * bytesperline + (x + 1) * channels; // C
			posD = y * bytesperline + (x - 1) * channels; // D
			posX = y * bytesperline + x * channels; // X

			if (datadst[posX] != 0)
			{
				if ((datadst[posA] == 0) && (datadst[posB] == 0) && (datadst[posC] == 0) && (datadst[posD] == 0))
				{
					datadst[posX] = label;
					labeltable[label] = label;
					label++;
				}
				else
				{
					num = 255;

					if (datadst[posA] != 0) num = labeltable[datadst[posA]];
					if ((datadst[posB] != 0) && (labeltable[datadst[posB]] < num)) num = labeltable[datadst[posB]];
					if ((datadst[posC] != 0) && (labeltable[datadst[posC]] < num)) num = labeltable[datadst[posC]];
					if ((datadst[posD] != 0) && (labeltable[datadst[posD]] < num)) num = labeltable[datadst[posD]];

					datadst[posX] = num;
					labeltable[num] = num;

					if (datadst[posA] != 0)
					{
						if (labeltable[datadst[posA]] != num)
						{
							for (tmplabel = labeltable[datadst[posA]], a = 1; a < label; a++)
							{
								if (labeltable[a] == tmplabel)
								{
									labeltable[a] = num;
								}
							}
						}
					}
					if (datadst[posB] != 0)
					{
						if (labeltable[datadst[posB]] != num)
						{
							for (tmplabel = labeltable[datadst[posB]], a = 1; a < label; a++)
							{
								if (labeltable[a] == tmplabel)
								{
									labeltable[a] = num;
								}
							}
						}
					}
					if (datadst[posC] != 0)
					{
						if (labeltable[datadst[posC]] != num)
						{
							for (tmplabel = labeltable[datadst[posC]], a = 1; a < label; a++)
							{
								if (labeltable[a] == tmplabel)
								{
									labeltable[a] = num;
								}
							}
						}
					}
					if (datadst[posD] != 0)
					{
						if (labeltable[datadst[posD]] != num)
						{
							for (tmplabel = labeltable[datadst[posD]], a = 1; a < label; a++)
							{
								if (labeltable[a] == tmplabel)
								{
									labeltable[a] = num;
								}
							}
						}
					}
				}
			}
		}
	}

	for (y = 1; y < height - 1; y++)
	{
		for (x = 1; x < width - 1; x++)
		{
			posX = y * bytesperline + x * channels; // X

			if (datadst[posX] != 0)
			{
				datadst[posX] = labeltable[datadst[posX]];
			}
		}
	}

	for (a = 1; a < label - 1; a++)
	{
		for (b = a + 1; b < label; b++)
		{
			if (labeltable[a] == labeltable[b]) labeltable[b] = 0;
		}
	}
	
	*nlabels = 0;
	for (a = 1; a < label; a++)
	{
		if (labeltable[a] != 0)
		{
			labeltable[*nlabels] = labeltable[a];
			(*nlabels)++; 
		}
	}

	if (*nlabels == 0) return NULL;

	blobs = (OVC*)calloc((*nlabels), sizeof(OVC));
	if (blobs != NULL)
	{
		for (a = 0; a < (*nlabels); a++) blobs[a].label = labeltable[a];
	}
	else return NULL;

	return blobs;
}

int vc_binary_blob_info(IVC* src, OVC* blobs, int nblobs)
{
	unsigned char* data = (unsigned char*)src->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int x, y, i;
	long int pos;
	int xmin, ymin, xmax, ymax;
	long int sumx, sumy;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if (channels != 1) return 0;

	for (i = 0; i < nblobs; i++)
	{
		xmin = width - 1;
		ymin = height - 1;
		xmax = 0;
		ymax = 0;

		sumx = 0;
		sumy = 0;

		blobs[i].area = 0;
		blobs[i].perimeter = 0;

		for (y = 1; y < height - 1; y++)
		{
			for (x = 1; x < width - 1; x++)
			{
				pos = y * bytesperline + x * channels;

				if (data[pos] == blobs[i].label)
				{
					blobs[i].area++;

					sumx += x;
					sumy += y;
					if (xmin > x) xmin = x;
					if (ymin > y) ymin = y;
					if (xmax < x) xmax = x;
					if (ymax < y) ymax = y;

					if ((data[pos - 1] != blobs[i].label) || (data[pos + 1] != blobs[i].label) || (data[pos - bytesperline] != blobs[i].label) || (data[pos + bytesperline] != blobs[i].label))
					{
						blobs[i].perimeter++;
					}
				}
			}
		}

		blobs[i].x = xmin;
		blobs[i].y = ymin;
		blobs[i].width = (xmax - xmin) + 1;
		blobs[i].height = (ymax - ymin) + 1;

		blobs[i].xc = sumx / MAX(blobs[i].area, 1);
		blobs[i].yc = sumy / MAX(blobs[i].area, 1);
	}

	return 1;
}


int vc_gray_histogram_equalization(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data;
	unsigned char* datadst = (unsigned char*)dst->data;
	int            width = src->width;
	int            height = src->height;
	int            bytesperline = src->bytesperline;
	int            channels = src->channels;
	int            i, x, y;

	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height))      return 0;
	if (channels != 1) return 0;

	int n = width * height;

	int ni[256] = { 0 };
	for (i = 0; i < n; ni[datasrc[i++]]++);

	float pdf[256];
	for (i = 0; i < 256; i++)
		pdf[i] = (float)ni[i] / (float)n;

	float cdf[256];
	cdf[0] = pdf[0];
	for (i = 1; i < 256; i++)
		cdf[i] = cdf[i - 1] + pdf[i];


	float cdfmin = 1.0f;
	for (i = 0; i < 256; i++)
	{
		if (cdf[i] > 0.0f && cdf[i] < cdfmin)
		{
			cdfmin = cdf[i];
			break;
		}
	}

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			long int pos = y * bytesperline + x * channels;
			int      pixel = datasrc[pos];

			float g = (cdf[pixel] - cdfmin) / (1.0f - cdfmin) * 255.0f;

			if (g < 0.0f) g = 0.0f;
			if (g > 255.0f) g = 255.0f;

			datadst[pos] = (unsigned char)g;
		}
	}

	return 1;
}


int vc_gray_lowpass_mean_filter(IVC* src, IVC* dst, int kernelsize) {
	unsigned char* datasrc = src->data;
	unsigned char* datadst = dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;

	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;
	if (kernelsize % 2 == 0) return 0; // kernel deve ser ímpar

	int offset = kernelsize / 2; // Para 3x3, offset = 1

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pos = y * bytesperline + x * channels;

			if (y < offset || y >= height - offset ||
				x < offset || x >= width - offset) {
				datadst[pos] = datasrc[pos];
				continue;
			}

			int sum = 0;
			for (int ky = -offset; ky <= offset; ky++) {
				for (int kx = -offset; kx <= offset; kx++) {
					int kpos = (y + ky) * bytesperline + (x + kx) * channels;
					sum += datasrc[kpos];
				}
			}

			datadst[pos] = (unsigned char)(sum / (kernelsize * kernelsize));
		}
	}

	return 1;
}


int vc_gray_lowpass_median_filter(IVC* src, IVC* dst, int kernelsize) {
	unsigned char* datasrc = src->data;
	unsigned char* datadst = dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;

	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;
	if (kernelsize % 2 == 0) return 0; // kernel deve ser ímpar

	int offset = kernelsize / 2;
	int kernelarea = kernelsize * kernelsize;

	unsigned char* neighborhood = (unsigned char*)malloc(kernelarea * sizeof(unsigned char));
	if (neighborhood == NULL) return 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pos = y * bytesperline + x * channels;

			if (y < offset || y >= height - offset ||
				x < offset || x >= width - offset) {
				datadst[pos] = datasrc[pos];
				continue;
			}

			int k = 0;
			for (int ky = -offset; ky <= offset; ky++) {
				for (int kx = -offset; kx <= offset; kx++) {
					int kpos = (y + ky) * bytesperline + (x + kx) * channels;
					neighborhood[k++] = datasrc[kpos];
				}
			}

			for (int i = 0; i < kernelarea - 1; i++) {
				for (int j = 0; j < kernelarea - i - 1; j++) {
					if (neighborhood[j] > neighborhood[j + 1]) {
						unsigned char tmp = neighborhood[j];
						neighborhood[j] = neighborhood[j + 1];
						neighborhood[j + 1] = tmp;
					}
				}
			}

			datadst[pos] = neighborhood[kernelarea / 2];
		}
	}

	free(neighborhood);
	return 1;
}