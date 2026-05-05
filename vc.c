//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//           INSTITUTO POLITÉCNICO DO CÁVADO E DO AVE
//                          2022/2023
//             ENGENHARIA DE SISTEMAS INFORMÁTICOS
//                    VISÃO POR COMPUTADOR
//
//             [  DUARTE DUQUE - dduque@ipca.pt  ]
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Desabilita (no MSVC++) warnings de funções não seguras (fopen, sscanf, etc...)
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "vc.h"

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//            FUNÇÕES: ALOCAR E LIBERTAR UMA IMAGEM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// Alocar memoria para uma imagem
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


// Libertar memoria de uma imagem
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


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//    FUNÇÕES: LEITURA E ESCRITA DE IMAGENS (PBM, PGM E PPM)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


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
				// Numa imagem PBM:
				// 1 = Preto
				// 0 = Branco
				//*p |= (datauchar[pos] != 0) << (8 - countbits);

				// Na nossa imagem:
				// 1 = Branco
				// 0 = Preto
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
				// Numa imagem PBM:
				// 1 = Preto
				// 0 = Branco
				//datauchar[pos] = (*p & (1 << (8 - countbits))) ? 1 : 0;

				// Na nossa imagem:
				// 1 = Branco
				// 0 = Preto
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

	// Abre o ficheiro
	if ((file = fopen(filename, "rb")) != NULL)
	{
		// Efectua a leitura do header
		netpbm_get_token(file, tok, sizeof(tok));

		if (strcmp(tok, "P4") == 0) { channels = 1; levels = 1; }	// Se PBM (Binary [0,1])
		else if (strcmp(tok, "P5") == 0) channels = 1;				// Se PGM (Gray [0,MAX(level,255)])
		else if (strcmp(tok, "P6") == 0) channels = 3;				// Se PPM (RGB [0,MAX(level,255)])
		else
		{
#ifdef VC_DEBUG
			printf("ERROR -> vc_read_image():\n\tFile is not a valid PBM, PGM or PPM file.\n\tBad magic number!\n");
#endif

			fclose(file);
			return NULL;
		}

		if (levels == 1) // PBM
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

			// Aloca memória para imagem
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
		else // PGM ou PPM
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

			// Aloca memória para imagem
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


//Gerar negativo da imagem Gray
int vc_gray_negative(IVC* srcdst)
{
	unsigned char* data = (unsigned char*)srcdst->data;
	int width = srcdst->width;
	int height = srcdst->height;
	int bytesperline = srcdst->width * srcdst->channels;
	int channels = srcdst->channels;
	int x, y;
	long int pos;

	//Verificacao de erros
	if ((srcdst->width <= 0) || (srcdst->height <= 0) || (srcdst->data == NULL)) return 0;
	if (channels != 1) return 0;

	//Inverte a imagem Gray
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

//Aula VC-4 RGB_To_Gray
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

	// Verificação de erros
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

		}
	}

	return 1;
}

//Aula VC-5 RGB_to_hsv
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

	// Verificação de erros
	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 3) || (dst->channels != 3)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y * bytesperline_src + x * channels_src;
			pos_dst = y * bytesperline_dst + x * channels_dst;

			// Normalizar RGB para [0,1]
			rf = datasrc[pos_src] / 255.0f;
			gf = datasrc[pos_src + 1] / 255.0f;
			bf = datasrc[pos_src + 2] / 255.0f;

			//Max e Min
			rgb_max = (rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf));
			rgb_min = (rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf));

			v = rgb_max;			// Value
			delta = rgb_max - rgb_min;

			//Saturação
			if (rgb_max == 0)
				s = 0;
			else
				s = delta / v;

			//Hue
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

//Aula VC-6 hsv_segmentation
int vc_hsv_segmentation(IVC* src, IVC* dst, int hmin_gimp, int hmax_gimp, int smin_gimp, int smax_gimp, int vmin_gimp, int vmax_gimp)
{
	// Converter valores do GIMP (H:0-360, S/V:0-100) para 0-255
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

			int h = datasrc[pos];       // Hue
			int s = datasrc[pos + 1];   // Saturation
			int v = datasrc[pos + 2];   // Value

			if ((h >= hmin && h <= hmax) &&
				(s >= smin && s <= smax) &&
				(v >= vmin && v <= vmax))
			{
				datadst[y * width + x] = 255;  // branco
			}
			else
			{
				datadst[y * width + x] = 0;    // preto
			}
		}
	}

	return 1;
}

//Aula VC-8 vc_scale_gray_to_color_palette
int vc_scale_gray_to_color_palette(IVC* src, IVC* dst)
{
	unsigned char* datasrc = (unsigned char*)src->data; //Imagem que recebo, e vejo os bytes dentro da imagem
	int bytesperline_src = src->width * src->channels; //Calcular a posição (literalmente bytes por linha)
	int channels_src = src->channels; //Serve para calcular a posição (se for 1 - é gray, 3 - recebe 3 cores) 

	unsigned char* datadst = (unsigned char*)dst->data;
	int bytesperline_dst = dst->width * dst->channels;
	int channels_dst = dst->channels;

	int width = src->width;
	int height = src->height;
	int x, y;
	long int pos_src, pos_dst;

	unsigned char gray;
	unsigned char r, g, b;

	// Verificação de erros
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

			// Palete
			if (gray < 64) { r = 0; g = (gray * 4); b = 255; }
			else if (gray < 128) { r = 0; g = 255; b = 255 - (gray - 64) * 4; }
			else if (gray < 192) { r = (gray - 128) * 4; g = 255; b = 0; }
			else { r = 255; g = 255 - ((gray - 192) * 4); b = 0; }

			// Guardar em RGB
			datadst[pos_dst] = r;
			datadst[pos_dst + 1] = g;
			datadst[pos_dst + 2] = b;
		}
	}

	return 1;
}

//Aula VC-9 vc_gray_to_binary
int vc_gray_to_binary(IVC* src, IVC* dst, int threshold)
{
	unsigned char* datasrc = (unsigned char*)src->data; //Imagem que recebo, e vejo os bytes dentro da imagem
	unsigned char* datadst = (unsigned char*)dst->data;

	int bytesperline_src = src->width * src->channels; //Calcular a posição (literalmente bytes por linha)
	int bytesperline_dst = dst->width * dst->channels;

	int channels_src = src->channels; //Serve para calcular a posição (se for 1 - é gray, 3 - recebe 3 cores)
	int channels_dst = dst->channels;

	int width = src->width;
	int height = src->height;

	int x, y;
	long int pos_src, pos_dst;

	// Verificação de erros
	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height)) return 0;
	if ((src->channels != 1) || (dst->channels != 1)) return 0;

	// Thresholding
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

//Aula VC-9
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

	// Verificação de erros
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

//Aula VC-10
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

	// Verificações de segurança
	if (src == NULL || dst == NULL) return 0;
	if (src->width != dst->width)  return 0;
	if (src->height != dst->height) return 0;
	if (src->channels != 1)         return 0;
	if (dst->channels != 1)         return 0;

	// Percorrer todos os pixéis da imagem
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			vmin = 255;
			vmax = 0;

			// Percorrer a vizinhança (kernel x kernel)
			for (ky = -half; ky <= half; ky++)
			{
				for (kx = -half; kx <= half; kx++)
				{
					int nx = x + kx;
					int ny = y + ky;

					// Verificar limites da imagem
					if (nx < 0) nx = 0;
					if (ny < 0) ny = 0;
					if (nx >= width)  nx = width - 1;
					if (ny >= height) ny = height - 1;

					int pixel = datasrc[ny * bytesperline + nx * channels];

					if (pixel < vmin) vmin = pixel;
					if (pixel > vmax) vmax = pixel;
				}
			}

			// Calcular threshold pelo método Midpoint: T = 1/2 * (vmin + vmax)
			threshold = (int)(0.5f * (vmin + vmax));

			// Pixel atual
			offset = y * bytesperline + x * channels;

			// Binarização
			if (datasrc[offset] > threshold)
				datadst[offset] = 255;
			else
				datadst[offset] = 0;
		}
	}

	return 1;
}

// Aula VC-11 
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

			// Percorrer vizinhança (kernel)
			for (ky = -offset; ky <= offset && !found; ky++)
			{
				for (kx = -offset; kx <= offset; kx++)
				{
					int xx = x + kx;
					int yy = y + ky;

					// Verificar limites
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

// Aula VC-11 
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

// Ex2 - gray to binary com intervalo 
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

// Ex2 - pega em imagem e coloca por cima
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

// Etiquetagem de blobs
// src		: Imagem bin?ria de entrada
// dst		: Imagem grayscale (ir? conter as etiquetas)
// nlabels	: Endere?o de mem?ria de uma vari?vel, onde ser? armazenado o n?mero de etiquetas encontradas.
// OVC*		: Retorna um array de estruturas de blobs (objectos), com respectivas etiquetas. ? necess?rio libertar posteriormente esta mem?ria.
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
	OVC* blobs; // Apontador para array de blobs (objectos) que ser? retornado desta fun??o.

	// Verifica??o de erros
	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if ((src->width != dst->width) || (src->height != dst->height) || (src->channels != dst->channels)) return NULL;
	if (channels != 1) return NULL;

	// Copia dados da imagem bin?ria para imagem grayscale
	memcpy(datadst, datasrc, bytesperline * height);

	// Todos os pix?is de plano de fundo devem obrigat?riamente ter valor 0
	// Todos os pix?is de primeiro plano devem obrigat?riamente ter valor 255
	// Ser?o atribu?das etiquetas no intervalo [1,254]
	// Este algoritmo est? assim limitado a 254 labels
	for (i = 0, size = bytesperline * height; i < size; i++)
	{
		if (datadst[i] != 0) datadst[i] = 255;
	}

	// Limpa os rebordos da imagem bin?ria
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

	// Efectua a etiquetagem
	for (y = 1; y < height - 1; y++)
	{
		for (x = 1; x < width - 1; x++)
		{
			// Kernel:
			// A B C
			// D X

			posA = (y - 1) * bytesperline + (x - 1) * channels; // A
			posB = (y - 1) * bytesperline + x * channels; // B
			posC = (y - 1) * bytesperline + (x + 1) * channels; // C
			posD = y * bytesperline + (x - 1) * channels; // D
			posX = y * bytesperline + x * channels; // X

			// Se o pixel foi marcado
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

					// Se A est? marcado
					if (datadst[posA] != 0) num = labeltable[datadst[posA]];
					// Se B est? marcado, e ? menor que a etiqueta "num"
					if ((datadst[posB] != 0) && (labeltable[datadst[posB]] < num)) num = labeltable[datadst[posB]];
					// Se C est? marcado, e ? menor que a etiqueta "num"
					if ((datadst[posC] != 0) && (labeltable[datadst[posC]] < num)) num = labeltable[datadst[posC]];
					// Se D est? marcado, e ? menor que a etiqueta "num"
					if ((datadst[posD] != 0) && (labeltable[datadst[posD]] < num)) num = labeltable[datadst[posD]];

					// Atribui a etiqueta ao pixel
					datadst[posX] = num;
					labeltable[num] = num;

					// Actualiza a tabela de etiquetas
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

	// Volta a etiquetar a imagem
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

	// Contagem do n?mero de blobs
	// Passo 1: Eliminar, da tabela, etiquetas repetidas
	for (a = 1; a < label - 1; a++)
	{
		for (b = a + 1; b < label; b++)
		{
			if (labeltable[a] == labeltable[b]) labeltable[b] = 0;
		}
	}
	// Passo 2: Conta etiquetas e organiza a tabela de etiquetas, para que n?o hajam valores vazios (zero) entre etiquetas
	*nlabels = 0;
	for (a = 1; a < label; a++)
	{
		if (labeltable[a] != 0)
		{
			labeltable[*nlabels] = labeltable[a]; // Organiza tabela de etiquetas
			(*nlabels)++; // Conta etiquetas
		}
	}

	// Se n?o h? blobs
	if (*nlabels == 0) return NULL;

	// Cria lista de blobs (objectos) e preenche a etiqueta
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

	// Verifica??o de erros
	if ((src->width <= 0) || (src->height <= 0) || (src->data == NULL)) return 0;
	if (channels != 1) return 0;

	// Conta ?rea de cada blob
	for (i = 0; i < nblobs; i++)
	{
		xmin = width - 1;
		ymin = height - 1;
		xmax = 0;
		ymax = 0;

		sumx = 0;
		sumy = 0;

		blobs[i].area = 0;

		for (y = 1; y < height - 1; y++)
		{
			for (x = 1; x < width - 1; x++)
			{
				pos = y * bytesperline + x * channels;

				if (data[pos] == blobs[i].label)
				{
					// ?rea
					blobs[i].area++;

					// Centro de Gravidade
					sumx += x;
					sumy += y;

					// Bounding Box
					if (xmin > x) xmin = x;
					if (ymin > y) ymin = y;
					if (xmax < x) xmax = x;
					if (ymax < y) ymax = y;

					// Per?metro
					// Se pelo menos um dos quatro vizinhos n?o pertence ao mesmo label, ent?o ? um pixel de contorno
					if ((data[pos - 1] != blobs[i].label) || (data[pos + 1] != blobs[i].label) || (data[pos - bytesperline] != blobs[i].label) || (data[pos + bytesperline] != blobs[i].label))
					{
						blobs[i].perimeter++;
					}
				}
			}
		}

		// Bounding Box
		blobs[i].x = xmin;
		blobs[i].y = ymin;
		blobs[i].width = (xmax - xmin) + 1;
		blobs[i].height = (ymax - ymin) + 1;

		// Centro de Gravidade
		//blobs[i].xc = (xmax - xmin) / 2;
		//blobs[i].yc = (ymax - ymin) / 2;
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

	// Verificação de erros
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

// Aula VC-15 Filtros no Dominio Espacial

int vc_gray_lowpass_mean_filter(IVC* src, IVC* dst, int kernelsize) {
	unsigned char* datasrc = src->data;
	unsigned char* datadst = dst->data;
	int width = src->width;
	int height = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;

	// Verificações de segurança
	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;
	if (kernelsize % 2 == 0) return 0; // kernel deve ser ímpar

	int offset = kernelsize / 2; // Para 3x3, offset = 1

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pos = y * bytesperline + x * channels;

			// Pixels de borda: copiar diretamente
			if (y < offset || y >= height - offset ||
				x < offset || x >= width - offset) {
				datadst[pos] = datasrc[pos];
				continue;
			}

			// Somar todos os pixels da vizinhança
			int sum = 0;
			for (int ky = -offset; ky <= offset; ky++) {
				for (int kx = -offset; kx <= offset; kx++) {
					int kpos = (y + ky) * bytesperline + (x + kx) * channels;
					sum += datasrc[kpos];
				}
			}

			// Dividir pelo número de elementos do kernel (normalização)
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

	// Verificações de segurança
	if (src->width <= 0 || src->height <= 0 || src->data == NULL) return 0;
	if (src->width != dst->width || src->height != dst->height) return 0;
	if (src->channels != 1 || dst->channels != 1) return 0;
	if (kernelsize % 2 == 0) return 0; // kernel deve ser ímpar

	int offset = kernelsize / 2;
	int kernelarea = kernelsize * kernelsize;

	// Array temporário para guardar os valores da vizinhança
	unsigned char* neighborhood = (unsigned char*)malloc(kernelarea * sizeof(unsigned char));
	if (neighborhood == NULL) return 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pos = y * bytesperline + x * channels;

			// Pixels de borda: copiar diretamente
			if (y < offset || y >= height - offset ||
				x < offset || x >= width - offset) {
				datadst[pos] = datasrc[pos];
				continue;
			}

			// Recolher os valores da vizinhança
			int k = 0;
			for (int ky = -offset; ky <= offset; ky++) {
				for (int kx = -offset; kx <= offset; kx++) {
					int kpos = (y + ky) * bytesperline + (x + kx) * channels;
					neighborhood[k++] = datasrc[kpos];
				}
			}

			// Ordenar o array (bubble sort)
			for (int i = 0; i < kernelarea - 1; i++) {
				for (int j = 0; j < kernelarea - i - 1; j++) {
					if (neighborhood[j] > neighborhood[j + 1]) {
						unsigned char tmp = neighborhood[j];
						neighborhood[j] = neighborhood[j + 1];
						neighborhood[j + 1] = tmp;
					}
				}
			}

			// Valor central = mediana
			datadst[pos] = neighborhood[kernelarea / 2];
		}
	}

	free(neighborhood);
	return 1;
}