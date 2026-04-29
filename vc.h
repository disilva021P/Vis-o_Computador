//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//           INSTITUTO POLIT�CNICO DO C�VADO E DO AVE
//                          2022/2023
//             ENGENHARIA DE SISTEMAS INFORM�TICOS
//                    VIS�O POR COMPUTADOR
//
//             [  DUARTE DUQUE - dduque@ipca.pt  ]
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


#define VC_DEBUG


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                   ESTRUTURA DE UMA IMAGEM
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


typedef struct {
	unsigned char *data;
	int width, height;
	int channels;			// Bin�rio/Cinzentos=1; RGB=3
	int levels;				// Bin�rio=1; Cinzentos [1,255]; RGB [1,255]
	int bytesperline;		// width * channels
} IVC;
typedef struct {
	int id;
	float area;
	float perimetro;
	float diametro_mm;
	char* calibre;			
	int categoria;				
} Laranja;
typedef struct {
    Laranja laranjas[1000];
    int count;              // laranjas na frame atual
    int total;              // contagem total desde frame 0
} FrameInfo;
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                    PROT�TIPOS DE FUN��ES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// FUN��ES: ALOCAR E LIBERTAR UMA IMAGEM
IVC *vc_image_new(int width, int height, int channels, int levels);
IVC *vc_image_free(IVC *image);

// FUN��ES: LEITURA E ESCRITA DE IMAGENS (PBM, PGM E PPM)
IVC *vc_read_image(char *filename);
int vc_write_image(char *filename, IVC *image);
int vc_gray_negative(IVC* srcdst);
int vc_gray_negative_colors(IVC* srcdst);
int vc_rgb_to_gray(IVC* src, IVC* dst);
int vc_rgb_to_hsv(IVC* src, IVC* dst);
int vc_hsv_segmentation(IVC* src, IVC* dst, int hmin, int hmax, int smin,int smax,int vmin, int vmax);
int calcula_area_branco(IVC* src,int* area);
int calcula_area_cores(IVC* src, int* area, 
	int hmin, int hmax, 
	int smin, int smax, 
	int vmin, int vmax);
int vc_scale_gray_to_color_palette(IVC *src, IVC *dst);
int vc_gray_to_binary_global_mean(IVC* src, IVC* dst);
int vc_gray_to_binary(IVC* src, IVC* dst, int threshold);
int vc_gray_to_binary_midpoint(IVC* src, IVC*dst, int kernel);
