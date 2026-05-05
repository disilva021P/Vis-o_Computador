#pragma once
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//           INSTITUTO POLIT?CNICO DO C?VADO E DO AVE
//                          2022/2023
//             ENGENHARIA DE SISTEMAS INFORM?TICOS
//                    VIS?O POR COMPUTADOR
//
//             [  DUARTE DUQUE - dduque@ipca.pt  ]
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


#define VC_DEBUG
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifdef __cplusplus
extern "C" {
#endif


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                   ESTRUTURA DE UMA IMAGEM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


typedef struct {
	unsigned char* data;
	int width, height;
	int channels;			// Bin?rio/Cinzentos=1; RGB=3
	int levels;				// Bin?rio=1; Cinzentos [1,255]; RGB [1,255]
	int bytesperline;		// width * channels
} IVC;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                   ESTRUTURA DE UM BLOB (OBJECTO)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

typedef struct {
	int x, y, width, height;	// Caixa Delimitadora (Bounding Box)
	int area;					// Area
	int xc, yc;					// Centro-de-massa
	int perimeter;				// Per?metro
	int label;					// Etiqueta
} OVC;



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                    PROT?TIPOS DE FUNCOES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// FUNCOES: ALOCAR E LIBERTAR UMA IMAGEM
IVC* vc_image_new(int width, int height, int channels, int levels);
IVC* vc_image_free(IVC* image);

// FUNCOES: LEITURA E ESCRITA DE IMAGENS (PBM, PGM E PPM)
IVC* vc_read_image(char* filename);
int vc_write_image(char* filename, IVC* image);

//Aula VC-4 - Declaração da função
int vc_rgb_negative(IVC* srcdst);

//Aula VC-4 - Declaração da função Inverter Gray
int vc_gray_negative(IVC* srcdst);


//VER MELHOR ---------------------------------

int vc_rgb_to_gray(IVC* src, IVC* dst);

//--------------------------------------------

//Aula VC-5 - Declaracão da funcao (rgb to hsv)
int vc_rgb_to_hsv(IVC* src, IVC* dst);

//Aula VC-6 - Declaração da função (hsv_segmentation)
int vc_hsv_segmentation(IVC* src, IVC* dst, int hmin, int hmax, int smin, int smax, int vmin, int vmax);

//Aula VC-7 - Declaração da função (Gray to color palette)
int vc_scale_gray_to_color_palette(IVC* src, IVC* dst);

//Aula VC-9 - Declaração da função (gray to binary, por thresholding manual)
int vc_gray_to_binary(IVC* src, IVC* dst, int threshold);

//Aula VC-9 - Declaração da função (gray to binary, por thresholding de média global)
int vc_gray_to_binary_global_mean(IVC* src, IVC* dst);

//Aula VC-10 - Declaração da função (gray to binary, por midpoint)
int vc_gray_to_binary_midpoint(IVC* src, IVC* dst, int kernel);

//Aula VC-11 - Declaração da função dilatação
int vc_binary_dilate(IVC* src, IVC* dst, int kernel);

//Aula VC-11 - Declaração da função erosão
int vc_binary_erode(IVC* src, IVC* dst, int kernel);

// Aula VC-12 - Ex2 
int vc_gray_to_binary_interval(IVC* src, IVC* dst, int tmin, int tmax);

int vc_apply_mask(IVC* src_gray, IVC* src_mask, IVC* dst);

// Aula VC-13 Labelling
OVC* vc_binary_blob_labelling(IVC* src, IVC* dst, int* nlabels);
int vc_binary_blob_info(IVC* src, OVC* blobs, int nblobs);

// Aula VC-14 Equalization Histogramas
int vc_gray_histogram_equalization(IVC* src, IVC* dst);

// Aula VC-15 Detecao de Contornos


// Aula VC-16 Filtro de Dominio Espacial
int vc_gray_lowpass_mean_filter(IVC* src, IVC* dst, int kernelsize);
int vc_gray_lowpass_median_filter(IVC* src, IVC* dst, int kernelsize);

#ifdef __cplusplus
}
#endif