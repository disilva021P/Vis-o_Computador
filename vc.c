//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//           INSTITUTO POLIT�CNICO DO C�VADO E DO AVE
//                          2022/2023
//             ENGENHARIA DE SISTEMAS INFORM�TICOS
//                    VIS�O POR COMPUTADOR
//
//             [  DUARTE DUQUE - dduque@ipca.pt  ]
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Desabilita (no MSVC++) warnings de fun��es n�o seguras (fopen, sscanf, etc...)
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "vc.h"


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//            FUN��ES: ALOCAR E LIBERTAR UMA IMAGEM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// Alocar mem�ria para uma imagem
IVC *vc_image_new(int width, int height, int channels, int levels)
{
	IVC *image = (IVC *) malloc(sizeof(IVC));

	if(image == NULL) return NULL;
	if((levels <= 0) || (levels > 255)) return NULL;

	image->width = width;
	image->height = height;
	image->channels = channels;
	image->levels = levels;
	image->bytesperline = image->width * image->channels;
	image->data = (unsigned char *) malloc(image->width * image->height * image->channels * sizeof(char));

	if(image->data == NULL)
	{
		return vc_image_free(image);
	}

	return image;
}


// Libertar mem�ria de uma imagem
IVC *vc_image_free(IVC *image)
{
	if(image != NULL)
	{
		if(image->data != NULL)
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
//    FUN��ES: LEITURA E ESCRITA DE IMAGENS (PBM, PGM E PPM)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


char *netpbm_get_token(FILE *file, char *tok, int len)
{
	char *t;
	int c;
	
	for(;;)
	{
		while(isspace(c = getc(file)));
		if(c != '#') break;
		do c = getc(file);
		while((c != '\n') && (c != EOF));
		if(c == EOF) break;
	}
	
	t = tok;
	
	if(c != EOF)
	{
		do
		{
			*t++ = c;
			c = getc(file);
		} while((!isspace(c)) && (c != '#') && (c != EOF) && (t - tok < len - 1));
		
		if(c == '#') ungetc(c, file);
	}
	
	*t = 0;
	
	return tok;
}


long int unsigned_char_to_bit(unsigned char *datauchar, unsigned char *databit, int width, int height)
{
	int x, y;
	int countbits;
	long int pos, counttotalbytes;
	unsigned char *p = databit;

	*p = 0;
	countbits = 1;
	counttotalbytes = 0;

	for(y=0; y<height; y++)
	{
		for(x=0; x<width; x++)
		{
			pos = width * y + x;

			if(countbits <= 8)
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
			if((countbits > 8) || (x == width - 1))
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


void bit_to_unsigned_char(unsigned char *databit, unsigned char *datauchar, int width, int height)
{
	int x, y;
	int countbits;
	long int pos;
	unsigned char *p = databit;

	countbits = 1;

	for(y=0; y<height; y++)
	{
		for(x=0; x<width; x++)
		{
			pos = width * y + x;

			if(countbits <= 8)
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
			if((countbits > 8) || (x == width - 1))
			{
				p++;
				countbits = 1;
			}
		}
	}
}


IVC *vc_read_image(char *filename)
{
	FILE *file = NULL;
	IVC *image = NULL;
	unsigned char *tmp;
	char tok[20];
	long int size, sizeofbinarydata;
	int width, height, channels;
	int levels = 255;
	int v;
	
	// Abre o ficheiro
	if((file = fopen(filename, "rb")) != NULL)
	{
		// Efectua a leitura do header
		netpbm_get_token(file, tok, sizeof(tok));

		if(strcmp(tok, "P4") == 0) { channels = 1; levels = 1; }	// Se PBM (Binary [0,1])
		else if(strcmp(tok, "P5") == 0) channels = 1;				// Se PGM (Gray [0,MAX(level,255)])
		else if(strcmp(tok, "P6") == 0) channels = 3;				// Se PPM (RGB [0,MAX(level,255)])
		else
		{
			#ifdef VC_DEBUG
			printf("ERROR -> vc_read_image():\n\tFile is not a valid PBM, PGM or PPM file.\n\tBad magic number!\n");
			#endif

			fclose(file);
			return NULL;
		}
		
		if(levels == 1) // PBM
		{
			if(sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &width) != 1 || 
			   sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &height) != 1)
			{
				#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tFile is not a valid PBM file.\n\tBad size!\n");
				#endif

				fclose(file);
				return NULL;
			}

			// Aloca mem�ria para imagem
			image = vc_image_new(width, height, channels, levels);
			if(image == NULL) return NULL;

			sizeofbinarydata = (image->width / 8 + ((image->width % 8) ? 1 : 0)) * image->height;
			tmp = (unsigned char *) malloc(sizeofbinarydata);
			if(tmp == NULL) return 0;

			#ifdef VC_DEBUG
			printf("\nchannels=%d w=%d h=%d levels=%d\n", image->channels, image->width, image->height, levels);
			#endif

			if((v = fread(tmp, sizeof(unsigned char), sizeofbinarydata, file)) != sizeofbinarydata)
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
			if(sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &width) != 1 || 
			   sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &height) != 1 || 
			   sscanf(netpbm_get_token(file, tok, sizeof(tok)), "%d", &levels) != 1 || levels <= 0 || levels > 255)
			{
				#ifdef VC_DEBUG
				printf("ERROR -> vc_read_image():\n\tFile is not a valid PGM or PPM file.\n\tBad size!\n");
				#endif

				fclose(file);
				return NULL;
			}

			// Aloca mem�ria para imagem
			image = vc_image_new(width, height, channels, levels);
			if(image == NULL) return NULL;

			#ifdef VC_DEBUG
			printf("\nchannels=%d w=%d h=%d levels=%d\n", image->channels, image->width, image->height, levels);
			#endif

			size = image->width * image->height * image->channels;

			if((v = fread(image->data, sizeof(unsigned char), size, file)) != size)
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


int vc_write_image(char *filename, IVC *image)
{
	FILE *file = NULL;
	unsigned char *tmp;
	long int totalbytes, sizeofbinarydata;
	
	if(image == NULL) return 0;

	if((file = fopen(filename, "wb")) != NULL)
	{
		if(image->levels == 1)
		{
			sizeofbinarydata = (image->width / 8 + ((image->width % 8) ? 1 : 0)) * image->height + 1;
			tmp = (unsigned char *) malloc(sizeofbinarydata);
			if(tmp == NULL) return 0;
			
			fprintf(file, "%s %d %d\n", "P4", image->width, image->height);
			
			totalbytes = unsigned_char_to_bit(image->data, tmp, image->width, image->height);
			printf("Total = %ld\n", totalbytes);
			if(fwrite(tmp, sizeof(unsigned char), totalbytes, file) != totalbytes)
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
		
			if(fwrite(image->data, image->bytesperline, image->height, file) != image->height)
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
int vc_gray_negative(IVC* srcdst){
	unsigned char *data = (unsigned char*) srcdst->data;
	int width = srcdst->width;
	int height  = srcdst->height;
	int bytesperline = srcdst->bytesperline;
	int channels = srcdst->channels;
	int x,y;
	long int pos;
	//Verificacao erros
	if((srcdst->width <=0 ) || (srcdst->height<=0) || (srcdst->data==NULL)) return 0;
	if (channels !=1) return 0;
	//inverte
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos= y*bytesperline + x*channels;
			data[pos] = 255 - data[pos];
		}
		
	}
	return 1;
	
}
int vc_gray_negative_colors(IVC* srcdst){
	unsigned char *data = (unsigned char*) srcdst->data;
	int width = srcdst->width;
	int height  = srcdst->height;
	int bytesperline = srcdst->bytesperline;
	int channels = srcdst->channels;
	int x,y;
	long int pos;
	//Verificacao erros
	if((srcdst->width <=0 ) || (srcdst->height<=0) || (srcdst->data==NULL)) return 0;
	if (channels !=3) return 0;
	//inverte
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos= y*bytesperline + x*channels;
			data[pos] = 255 - data[pos];
			data[pos+1] = 255 - data[pos+1];
			data[pos+2] = 255 - data[pos+2];
		}
	}
	return 1;
	
}
int vc_rgb_to_gray(IVC* src, IVC* dst){
	unsigned char *data = (unsigned char*) src->data;
	int width = src->width;
	int height  = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int x,y;
	long int pos_src,pos_dst;
	float rf, gf, bf;
	//Verificacao erros
	if((src->width <=0 ) || (src->height<=0) || (src->data==NULL)) return 0;
	if((src->width != dst->width ) || (src->height!= dst->height)) return 0;
	if ((channels !=3) || (dst->channels!=1)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos_src = y*bytesperline + x*channels;
			pos_dst = y*dst->bytesperline + x*dst->channels;
			rf= (float) src->data[pos_src];
			gf= (float) src->data[pos_src+1];
			bf= (float) src->data[pos_src+2];
			dst->data[pos_dst] = (unsigned char) ((rf * 0.299) + (gf*0.587)+ (bf*0114));
		}
	}
	return 1;
}

int vc_rgb_to_hsv(IVC* src, IVC* dst){
	unsigned char *data = (unsigned char*) src->data;
	int width = src->width;
	int height  = src->height;
	int bytesperline = src->bytesperline;
	int channels = src->channels;
	int x,y;
	long int pos;
	float rf, gf, bf;
	float max,min,h,s,v;
	//Verificacao erros
	if((src->width <=0 ) || (src->height<=0) || (src->data==NULL)) return 0;
	if((src->width != dst->width ) || (src->height!= dst->height)) return 0;
	if ((channels !=3) || (dst->channels!=3)) return 0;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			pos = y * bytesperline + x * channels;

			rf= (float) src->data[pos];
			gf= (float) src->data[pos+1];
			bf= (float) src->data[pos+2];

			if(rf>=gf && rf>=bf) max=rf;
			else if(gf>=rf && gf>=bf) max= gf;
			else if(bf>=gf && bf>=rf) max=bf;
			if(rf<=gf && rf<=bf) min=rf;
			else if(gf<=rf && gf<=bf) min= gf;
			else if(bf<=gf && bf<=rf) min=bf;
			v=max;
			if(max==0) s=0;
			else s=(max-min)/max;
			if (max == min) {
				h = 0;
			} 
			if(max==rf && gf>=bf) h= 60*(gf-bf)/(max-min);
			if(max==rf && gf<bf) h= 360+60*(gf-bf)/(max-min);
			if(max==gf) h=120+60*(bf-rf)/(max-min);
			if(max==bf) h=240+60*(rf-gf)/(max-min);
			dst->data[pos]     = (unsigned char)(h / 360.0f * 255);
			dst->data[pos + 1] = (unsigned char)(s * 255);
			dst->data[pos + 2] = (unsigned char)(v);
		}
	}
	return 1;
}
int vc_hsv_segmentation(IVC* src, IVC* dst,
	int hmin, int hmax,
	int smin, int smax,
	int vmin, int vmax)
{
	unsigned char *data_src = (unsigned char*) src->data;
	unsigned char *data_dst = (unsigned char*) dst->data;

	int width = src->width;
	int height = src->height;
	int bytesperline_src = src->bytesperline;
	int bytesperline_dst = dst->bytesperline;

	int x, y;
	long int pos_src, pos_dst;

	float h, s, v;

	if ((width <= 0) || (height <= 0) || (data_src == NULL)) return 0;
	if ((width != dst->width) || (height != dst->height)) return 0;
	if ((src->channels != 3) || (dst->channels != 1)) return 0;

	// Converter limites para escala 0–255
	int h_min = (int)((hmin / 360.0f) * 255.0f);
    int h_max = (int)((hmax / 360.0f) * 255.0f);
    int s_min = (int)((smin / 100.0f) * 255.0f);
    int s_max = (int)((smax / 100.0f) * 255.0f);
    int v_min = (int)((vmin / 100.0f) * 255.0f);
    int v_max = (int)((vmax / 100.0f) * 255.0f);

	for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            pos_src = y * bytesperline_src + x * src->channels;
            pos_dst = y * bytesperline_dst + x;

            // Pegamos os valores como inteiros para comparar rápido
            int h = data_src[pos_src];
            int s = data_src[pos_src + 1];
            int v = data_src[pos_src + 2];

            // Lógica de segmentação
            if (h >= h_min && h <= h_max &&
                s >= s_min && s <= s_max &&
                v >= v_min && v <= v_max)
            {
                data_dst[pos_dst] = 255; // Branco (Laranja detectado)
            }
            else
            {
                data_dst[pos_dst] = 0;   // Preto
            }
        }
    }
	return 1;
}
int calcula_area_branco(IVC* src,int* area)
{
	unsigned char *data_src = (unsigned char*) src->data;
	int width = src->width;
	int height = src->height;
	int bytesperline_src = src->bytesperline;

	int x, y;
	long int pos_src, pos_dst;

	float h, s, v;

	if ((width <= 0) || (height <= 0) || (data_src == NULL)) return 0;
	if ((src->channels != 1)) return 0;



	for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            pos_src = y * bytesperline_src + x * src->channels;

            if(data_src[pos_src]){
				(*area)++;
			}
        }
    }
	return 1;
}
int calcula_area_cores(IVC* src, int* area, 
	int hmin, int hmax, 
	int smin, int smax, 
	int vmin, int vmax)
{
	unsigned char *data_src = (unsigned char*) src->data;
	int width = src->width;
	int height = src->height;
	int bytesperline_src = src->bytesperline;
	int x, y;
	long int pos_src;

	// Verificações de segurança
	if ((width <= 0) || (height <= 0) || (data_src == NULL) || (area == NULL)) return 0;

	if (src->channels != 3) return 0;

	// Inicializar a contagem
	*area = 0;

	int h_min = (int)((hmin / 360.0f) * 255.0f);
	int h_max = (int)((hmax / 360.0f) * 255.0f);
	int s_min = (int)((smin / 100.0f) * 255.0f);
	int s_max = (int)((smax / 100.0f) * 255.0f);
	int v_min = (int)((vmin / 100.0f) * 255.0f);
	int v_max = (int)((vmax / 100.0f) * 255.0f);

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
				
			pos_src = y * bytesperline_src + x * 3;

			int h = data_src[pos_src];     
			int s = data_src[pos_src + 1]; 
			int v = data_src[pos_src + 2]; 


			if (h >= h_min && h <= h_max &&
			s >= s_min && s <= s_max &&
			v >= v_min && v <= v_max)
			{
				(*area)++; 
			}
		}
	}
	return 1;
}
int vc_scale_gray_to_color_palette(IVC *src, IVC *dst){
		unsigned char *data = (unsigned char*) src->data;
		int width = src->width;
		int height  = src->height;
		int bytesperline = src->bytesperline;
		int channels = src->channels;
		int x,y;
		long int pos_src,pos_dst;
		float rf, gf, bf;
		//Verificacao erros
		if((src->width <=0 ) || (src->height<=0) || (src->data==NULL)) return 0;
		if((src->width != dst->width ) || (src->height!= dst->height)) return 0;
		if ((channels !=1) || (dst->channels!=3)) return 0;
	
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				pos_src = y*bytesperline + x*channels;
				pos_dst = y*dst->bytesperline + x*dst->channels;
				int grey= data[pos_src];
				int r,g,b;
				if(grey<64){
					r=0;
					g=grey*4;
					b=255;
				}else if(grey<128){
					r=0;
					g=255;
					b=255-(grey-64)*4;
				}else if(grey<192){
					r=(grey-128)*4;
					g=255;
					b=0;
				}
				else if(grey<=255){
					r=255;
					g=255-(grey-192)*4;
					b=0;
				}
				dst->data[pos_dst]=r;
				dst->data[pos_dst+1]=g;
				dst->data[pos_dst+2]=b;
			}
		}
		return 1;
		
}

int vc_gray_to_binary(IVC* src, IVC* dst, int threshold) 
{
	unsigned char* datasrc = (unsigned char*)src->data; //Imagem que recebo, e vejo os bytes dentro da imagem
	unsigned char* datadst = (unsigned char*)dst->data;

	int bytesperline_src = src->width * src->channels; //Calcular a posi��o (literalmente bytes por linha)
	int bytesperline_dst = dst->width * dst->channels;

	int channels_src = src->channels; //Serve para calcular a posi��o (se for 1 - � gray, 3 - recebe 3 cores)
	int channels_dst = dst->channels;

	int width = src->width;
	int height = src->height;

	int x, y;
	long int pos_src, pos_dst;

	// Verifica��o de erros
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

	// Verifica��o de erros
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

int vc_gray_to_binary_midpoint(IVC* src, IVC*dst, int kernel){
	unsigned char *srcdata = (unsigned char*) src->data;
	unsigned char *dstdata = (unsigned char*) dst->data;
	if((src->width <=0 ) || (src->height<=0) || (src->data==NULL)) return 0;
		if((src->width != dst->width ) || (src->height!= dst->height)) return 0;
		if ((src->channels !=1) || (dst->channels!=1)) return 0;
	int width= src->width;	
	int height= src->height;
	int y,x,pos;
	int treshhold;
	int viagem = kernel/2;
	int vmin, vmax, new_pos;
	for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				pos = y*src->bytesperline + x*src->channels;
				for(int h=-(viagem-y);h<=y+viagem;h++){
					for(int w=-viagem;w<=viagem;w++){
						if(h>0 && h<src->height && w>h && w<src->width){
							new_pos = (y+h)*src->bytesperline + (x+w)*src->channels;
							if(src->data[new_pos]>vmax) vmax= src->data[new_pos];
							if(src->data[new_pos]<vmin) vmin= src->data[new_pos];
						}
						
			
					}
				}
				treshhold= (vmin+vmax)/2;
			}
		}
		return 1;
}

static int nLaranjas;
Laranja